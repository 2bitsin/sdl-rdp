#include "_detail/state.hpp"
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <freerdp/codec/color.h>
#include <algorithm>
#include <span>
#include <array>
#include "_detail/copy-rows.hpp"
#include "_detail/scaling.hpp"
namespace Backend {
namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;
struct Frame { sdlrdp_rect area; std::span<BYTE> pixels; };

auto SendSurfaceBits(rdpUpdate* update, Frame const& what, Encoder const& encoder) -> bool
{
  Expects(update && update->SurfaceBits, "surface callback exists");
  auto payload = what.pixels;
  auto command = SURFACE_BITS_COMMAND{ };
  command.cmdType = CMDTYPE_SET_SURFACE_BITS;
  command.skipCompression = TRUE;
  command.destLeft = what.area.x;
  command.destTop = what.area.y;
  command.destRight = (what.area.x + what.area.w);
  command.destBottom = (what.area.y + what.area.h);
  command.bmp.bpp = 32u;
  command.bmp.codecID = encoder.Id(update->context->settings);
  command.bmp.width = static_cast<UINT16>(what.area.w);
  command.bmp.height = static_cast<UINT16>(what.area.h);
  command.bmp.bitmapDataLength = static_cast<UINT32>(payload.size());
  command.bmp.bitmapData = payload.data();
  return update->SurfaceBits(update->context, &command);
}

auto BitmapRectangle(Frame const& band, bool compressed) -> BITMAP_DATA
{
  Expects(!band.pixels.empty(), "bitmap payload exists");
  auto payload = band.pixels;
  auto rectangle = BITMAP_DATA{ };
  rectangle.destLeft = band.area.x;
  rectangle.destTop = band.area.y;
  // Bitmap update corners are inclusive, unlike a surface command's.
  rectangle.destRight = (band.area.x + band.area.w) - 1u;
  rectangle.destBottom = (band.area.y + band.area.h) - 1u;
  rectangle.width = band.area.w;
  rectangle.height = band.area.h;
  rectangle.bitsPerPixel = 32u;
  rectangle.bitmapLength = static_cast<UINT32>(payload.size());
  rectangle.bitmapDataStream = payload.data();
  rectangle.compressed = compressed;
  rectangle.cbScanWidth = band.area.w * 4;
  rectangle.cbUncompressedSize = band.area.w * band.area.h * 4;
  rectangle.cbCompMainBodySize = payload.size();
  return rectangle;
}
bool SendBitmapBand(rdpUpdate* update, std::span<BITMAP_DATA> rectangles)
{
  Expects(update && update->BitmapUpdate && !rectangles.empty(), "bitmap batch exists");
  auto batch = BITMAP_UPDATE{ };
  batch.number = rectangles.size();
  batch.rectangles = rectangles.data();
  batch.skipCompression = TRUE;
  return update->BitmapUpdate(update->context, &batch);
}

auto Snapshot(Peer& peer, sdlrdp_rect area, std::span<BYTE> buffer, bool flip) -> Frame
{
  Expects(area.w > 0 && area.h > 0, "damage has positive extent");
  auto stride = std::size_t(area.w) * 4;
  Expects(stride * area.h <= buffer.size(), "wire band fits scratch storage");
  // Uncompressed RDP bitmap rows travel bottom-up (MS-RDPBCGR 2.2.9.1.1.3.1.2.2).
  if (peer.desktop.w != int(peer.snapshot_width) || peer.desktop.h != int(peer.snapshot_height))
    ScaleBand(peer, area, buffer, flip);
  else CopyRows(std::span<BYTE const>(*peer.snapshot).subspan(
    (std::size_t(area.y) * peer.snapshot_width + area.x) * 4), peer.snapshot_width * 4,
    buffer, stride, area.h, stride, flip);
  return {area, buffer.first(stride * area.h)};
}
bool SelectEncoder(Peer& peer)
{
  Expects(peer.client && peer.client->context, "peer settings exist");
  auto previous = peer.encoder.codec;
  if (!peer.encoder.Select(peer.client->context->settings, peer.owner.codec.load())) return false;
  if (previous != peer.encoder.codec && peer.active)
    peer.owner.Push({.type = SDLRDP_CODEC_CHANGED, .codec_changed = {peer.encoder.codec}});
  return true;
}
bool SendConverted(rdpUpdate* update, Frame band, unsigned depth)
{
  Expects(depth == 16 || depth == 24, "supported packed colour depth");
  auto format = depth == 16 ? PIXEL_FORMAT_RGB16 : PIXEL_FORMAT_BGR24;
  auto stride = (unsigned(band.area.w) * (depth / 8) + 3) & ~3u;
  std::vector<BYTE> converted(std::size_t(stride) * band.area.h);
  if (!freerdp_image_copy(converted.data(), format, stride, 0, 0, band.area.w, band.area.h,
      band.pixels.data(), PIXEL_FORMAT_BGRX32, band.area.w * 4, 0, 0, nullptr, FREERDP_FLIP_NONE)) return false;
  band.pixels = converted;
  auto rectangle = BitmapRectangle(band, false);
  rectangle.bitsPerPixel = depth;
  rectangle.width = stride / (depth / 8);
  rectangle.cbScanWidth = stride;
  rectangle.cbUncompressedSize = converted.size();
  return SendBitmapBand(update, {&rectangle, 1});
}
bool Transmit(Peer& peer, Frame band)
{
  Expects(peer.client && peer.client->context, "peer transport exists");
  if (band.pixels.empty()) return true;
  auto& encoder = peer.encoder;
  auto context = peer.client->context;
  auto depth = freerdp_settings_get_uint32(context->settings, FreeRDP_ColorDepth);
  if (depth != 32) return SendConverted(context->update, band, depth);
  if (encoder.codec != SDLRDP_CODEC_RAW) {
    if (!encoder.Encode(band.pixels, band.area.w, band.area.h)) return false;
    band.pixels = encoder.payload;
  }
  auto surface = encoder.codec != SDLRDP_CODEC_PLANAR
    && freerdp_settings_get_bool(context->settings, FreeRDP_SurfaceCommandsEnabled);
  if (surface) return SendSurfaceBits(context->update, band, encoder);
  auto rectangle = BitmapRectangle(band, false);
  return SendBitmapBand(context->update, {&rectangle, 1});
}

bool SendPlanar(Peer& peer, sdlrdp_rect area)
{
  Expects(area.w > 0 && area.h > 0, "planar damage exists");
  auto& encoder = peer.encoder;
  std::vector<BYTE> payload;
  payload.reserve(BITMAP_RECTANGLE_LIMIT);
  std::vector<BITMAP_DATA> rectangles;
  encoder.scratch.resize(std::size_t(area.w) * 4);
  std::size_t wire_size = 4;
  for (int y = peer.row; y < area.h; ++y) {
    auto row = Snapshot(peer, {area.x, area.y + y, area.w, 1}, encoder.scratch, false);
    if (row.pixels.empty()) break;
    if (!encoder.Encode(row.pixels, row.area.w, 1)) return false;
    auto size = encoder.payload.size();
    if (wire_size + 26 + size > BITMAP_RECTANGLE_LIMIT && !rectangles.empty()) {
      if (!SendBitmapBand(peer.client->context->update, rectangles)) return false;
      rectangles.clear();
      payload.clear();
      wire_size = 4;
      if (peer.client->IsWriteBlocked(peer.client.get())) {
        peer.row = y;
        return true;
      }
    }
    auto offset = payload.size();
    payload.insert(payload.end(), encoder.payload.begin(), encoder.payload.end());
    row.pixels = std::span(payload).subspan(offset, size);
    rectangles.push_back(BitmapRectangle(row, true));
    wire_size += 26 + size;
  }
  peer.row = area.h;
  return rectangles.empty() || SendBitmapBand(peer.client->context->update, rectangles);
}

}
bool SendFrame(Peer& peer)
{
  Expects(peer.snapshot != nullptr, "immutable frame exists");
  auto& encoder = peer.encoder;
  if (!peer.frame_started) {
    if (!SelectEncoder(peer) || !peer.Marker(SURFACECMD_FRAMEACTION_BEGIN)) return false;
    peer.frame_started = true;
  }
  while (peer.rect_index < peer.sending.rects.size()) {
    if (peer.client->IsWriteBlocked(peer.client.get())) return true;
    auto area = ScaleDamage(peer.sending.rects[peer.rect_index], peer);
    if (encoder.codec == SDLRDP_CODEC_PLANAR) {
      if (!SendPlanar(peer, area)) return false;
    } else {
      auto lines = encoder.codec == SDLRDP_CODEC_REMOTEFX ? 64
        : std::max(1, int((BITMAP_RECTANGLE_LIMIT - 1024) / (std::size_t(area.w) * 4)));
      auto height = std::min(lines, area.h - int(peer.row));
      encoder.scratch.resize(std::size_t(area.w) * height * 4);
      auto band = Snapshot(peer, {area.x, area.y + int(peer.row), area.w, height},
        encoder.scratch, encoder.codec == SDLRDP_CODEC_RAW);
      if (!Transmit(peer, band)) return false;
      peer.row += height;
    }
    if (peer.row == unsigned(area.h)) { ++peer.rect_index; peer.row = 0; }
  }
  if (!peer.Marker(SURFACECMD_FRAMEACTION_END)) return false;
  std::scoped_lock lock(peer.owner.frame_guard);
  peer.FrameSent();
  peer.snapshot.reset();
  peer.sending.clear();
  return true;
}
}
