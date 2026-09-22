#include "_detail/state.hpp"
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <algorithm>
#include <span>
#include <array>
#include "_detail/copy-rows.hpp"
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

auto Snapshot(State& state, sdlrdp_rect area, std::span<BYTE> buffer, bool flip) -> Frame
{
  Expects(area.w > 0 && area.h > 0, "damage has positive extent");
  std::scoped_lock lock(state.frame_guard);
  area.w = std::min(area.w, int(state.frame_width) - area.x);
  area.h = std::min(area.h, int(state.frame_height) - area.y);
  if (area.w <= 0 || area.h <= 0) return {{}, {}};
  auto stride = std::size_t(area.w) * 4;
  Expects(stride * area.h <= buffer.size(), "wire band fits scratch storage");
  // Uncompressed RDP bitmap rows travel bottom-up (MS-RDPBCGR 2.2.9.1.1.3.1.2.2).
  CopyRows(std::span<BYTE const>(state.shadow).subspan(
    (std::size_t(area.y) * state.frame_width + area.x) * 4), state.frame_width * 4,
    buffer, stride, area.h, stride);
  auto rows = buffer.first(stride * area.h) | std::views::chunk(stride);
  for (auto index : std::views::iota(0, flip ? area.h / 2 : 0))
    std::ranges::swap_ranges(rows[index], (rows | std::views::reverse)[index]);
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
bool Transmit(Peer& peer, Frame band)
{
  Expects(peer.client && peer.client->context, "peer transport exists");
  if (band.pixels.empty()) return true;
  auto& encoder = peer.encoder;
  auto context = peer.client->context;
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

bool SendPlanar(Peer& peer, State& state, sdlrdp_rect area)
{
  Expects(area.w > 0 && area.h > 0, "planar damage exists");
  auto& encoder = peer.encoder;
  std::vector<BYTE> payload;
  payload.reserve(BITMAP_RECTANGLE_LIMIT);
  std::vector<BITMAP_DATA> rectangles;
  encoder.scratch.resize(std::size_t(area.w) * 4);
  std::size_t wire_size = 4;
  for (int y = 0; y < area.h; ++y) {
    auto row = Snapshot(state, {area.x, area.y + y, area.w, 1}, encoder.scratch, false);
    if (row.pixels.empty()) break;
    if (!encoder.Encode(row.pixels, row.area.w, 1)) return false;
    auto size = encoder.payload.size();
    if (wire_size + 26 + size > BITMAP_RECTANGLE_LIMIT && !rectangles.empty()) {
      if (!SendBitmapBand(peer.client->context->update, rectangles)) return false;
      rectangles.clear();
      payload.clear();
      wire_size = 4;
      if (peer.client->IsWriteBlocked(peer.client.get())) {
        std::scoped_lock lock(state.frame_guard);
        peer.Post({area.x, area.y + y, area.w, area.h - y});
        return true;
      }
    }
    auto offset = payload.size();
    payload.insert(payload.end(), encoder.payload.begin(), encoder.payload.end());
    row.pixels = std::span(payload).subspan(offset, size);
    rectangles.push_back(BitmapRectangle(row, true));
    wire_size += 26 + size;
  }
  return rectangles.empty() || SendBitmapBand(peer.client->context->update, rectangles);
}

}
bool SendFrame(rdpContext* context, State& state, sdlrdp_rect area)
{
  Expects(context && area.w > 0 && area.h > 0, "frame destination and damage exist");
  auto& peer = Peer::Held(context->peer);
  auto& encoder = peer.encoder;
  if (!SelectEncoder(peer)) return false;
  if (encoder.codec == SDLRDP_CODEC_PLANAR) return SendPlanar(peer, state, area);
  auto rfx = encoder.codec == SDLRDP_CODEC_REMOTEFX;
  auto lines = rfx ? 64 : int((BITMAP_RECTANGLE_LIMIT - 1024) / (std::size_t(area.w) * 4));
  lines = std::max(1, lines);
  encoder.scratch.resize(std::size_t(area.w) * lines * 4);
  auto buffer = std::span(encoder.scratch);
  for (int first = 0; first < area.h; first += lines) {
    if (context->peer->IsWriteBlocked(context->peer)) {
      std::scoped_lock lock(state.frame_guard);
      peer.Post({area.x, area.y + first, area.w, area.h - first});
      return true;
    }
    auto band = Snapshot(state, {area.x, area.y + first, area.w,
      std::min(lines, area.h - first)}, buffer,
      encoder.codec == SDLRDP_CODEC_RAW);
    if (!Transmit(peer, band)) return false;
  }
  return true;
}
}
