#include "_detail/state.hpp"
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <freerdp/codec/color.h>
#include <algorithm>
#include <span>
#include <array>
#include "_detail/scaling.hpp"
namespace Backend {
namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;

auto SendSurfaceBits(rdpUpdate *update, Frame const &what, unsigned codec) -> bool {
  Expects(update != nullptr, "update exists");
  Expects(update->SurfaceBits != nullptr, "surface callback exists");
  auto payload = what.pixels;
  auto command = SURFACE_BITS_COMMAND{};
  command.cmdType = CMDTYPE_SET_SURFACE_BITS;
  command.skipCompression = TRUE;
  command.destLeft = what.area.x;
  command.destTop = what.area.y;
  command.destRight = (what.area.x + what.area.w);
  command.destBottom = (what.area.y + what.area.h);
  command.bmp.bpp = 32u;
  command.bmp.codecID = codec;
  command.bmp.width = static_cast<UINT16>(what.area.w);
  command.bmp.height = static_cast<UINT16>(what.area.h);
  command.bmp.bitmapDataLength = static_cast<UINT32>(payload.size());
  command.bmp.bitmapData = payload.data();
  return update->SurfaceBits(update->context, &command);
}

auto BitmapRectangle(Frame const &band, bool compressed) -> BITMAP_DATA {
  Expects(!band.pixels.empty(), "bitmap payload exists");
  auto payload = band.pixels;
  auto rectangle = BITMAP_DATA{};
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
bool SendBitmapBand(rdpUpdate *update, std::span<BITMAP_DATA> rectangles) {
  Expects(update != nullptr, "update exists");
  Expects(update->BitmapUpdate != nullptr, "bitmap callback exists");
  Expects(!rectangles.empty(), "bitmap batch exists");
  auto batch = BITMAP_UPDATE{};
  batch.number = rectangles.size();
  batch.rectangles = rectangles.data();
  batch.skipCompression = TRUE;
  return update->BitmapUpdate(update->context, &batch);
}

bool SelectEncoder(Peer &peer) {
  Expects(peer.client != nullptr, "peer exists");
  Expects(peer.client->context != nullptr, "peer settings exist");
  auto previous = peer.encoder.codec;
  if (!peer.encoder.Select(peer.client->context->settings, peer.owner.codec.load()))
    return false;
  if (previous != peer.encoder.codec && peer.active)
    peer.owner.Push({.type = SDLRDP_CODEC_CHANGED, .codec_changed = {peer.encoder.codec}});
  return true;
}
auto Convert(Frame band, unsigned depth) -> std::vector<BYTE> {
  Expects(depth == 16 || depth == 24, "supported packed colour depth");
  auto format = depth == 16 ? PIXEL_FORMAT_RGB16 : PIXEL_FORMAT_BGR24;
  auto stride = (unsigned(band.area.w) * (depth / 8) + 3) & ~3u;
  std::vector<BYTE> converted(std::size_t(stride) * band.area.h);
  if (!freerdp_image_copy(converted.data(), format, stride, 0, 0, band.area.w, band.area.h,
                          band.pixels.data(), PIXEL_FORMAT_BGRX32, band.area.w * 4, 0, 0, nullptr, FREERDP_FLIP_NONE))
    return {};
  return converted;
}
} // namespace
bool LegacyFrame::Prepare(Peer &peer) {
  Expects(peer.snapshot != nullptr, "immutable frame exists");
  packets.clear();
  next.reset();
  if (!SelectEncoder(peer))
    return false;
  auto settings = peer.client->context->settings;
  depth = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
  wire = depth != 32 ? Wire::Bitmap : peer.encoder.codec == SDLRDP_CODEC_PLANAR                         ? Wire::Planar
                                  : freerdp_settings_get_bool(settings, FreeRDP_SurfaceCommandsEnabled) ? Wire::Surface
                                                                                                        : Wire::Bitmap;
  codec = wire == Wire::Surface ? peer.encoder.Id(settings) : 0;
  return true;
}
bool LegacyFrame::Planar(Peer &peer, sdlrdp_rect area) {
  Expects(area.w > 0, "planar width exists");
  Expects(area.h > 0, "planar height exists");
  auto &encoder = peer.encoder;
  Packet packet;
  std::size_t wire_size = 4;
  encoder.scratch.resize(std::size_t(area.w) * 4);
  for (int y = 0; y < area.h; ++y) {
    auto row = Snapshot(peer, {area.x, area.y + y, area.w, 1}, encoder.scratch, false);
    if (!encoder.Encode(row.pixels, row.area.w, 1))
      return false;
    auto size = encoder.payload.size();
    if (wire_size + 26 + size > BITMAP_RECTANGLE_LIMIT && !packet.bands.empty()) {
      packets.push_back(std::move(packet));
      packet = {};
      wire_size = 4;
    }
    packet.bands.push_back({row.area, {encoder.payload.begin(), encoder.payload.end()}});
    wire_size += 26 + size;
  }
  if (!packet.bands.empty())
    packets.push_back(std::move(packet));
  return true;
}
bool LegacyFrame::Bands(Peer &peer, sdlrdp_rect area) {
  auto &encoder = peer.encoder;
  auto lines = encoder.codec == SDLRDP_CODEC_REMOTEFX ? 64
                                                      : std::max(1, int((BITMAP_RECTANGLE_LIMIT - 1024) / (std::size_t(area.w) * 4)));
  for (int row = 0; row < area.h; row += lines) {
    auto height = std::min(lines, area.h - row);
    encoder.scratch.resize(std::size_t(area.w) * height * 4);
    auto band = Snapshot(peer, {area.x, area.y + row, area.w, height},
                         encoder.scratch, encoder.codec == SDLRDP_CODEC_RAW);
    if (depth != 32) {
      auto converted = Convert(band, depth);
      if (converted.empty())
        return false;
      packets.push_back({{Band{band.area, std::move(converted)}}, {}});
      continue;
    }
    if (encoder.codec != SDLRDP_CODEC_RAW) {
      if (!encoder.Encode(band.pixels, band.area.w, band.area.h))
        return false;
      band.pixels = encoder.payload;
    }
    packets.push_back({{Band{band.area, {band.pixels.begin(), band.pixels.end()}}}, {}});
  }
  return true;
}
bool LegacyFrame::Encode(Peer &peer) {
  Expects(peer.snapshot != nullptr, "immutable frame exists");
  auto encoded = std::ranges::all_of(peer.sending.rects, [&](auto rect) {
    auto area = ScaleDamage(rect, peer);
    return wire == Wire::Planar ? Planar(peer, area) : Bands(peer, area);
  });
  if (encoded)
    std::ranges::for_each(packets, [&](auto &packet) { Describe(packet); });
  return encoded;
}
void LegacyFrame::Describe(Packet &packet) {
  if (wire == Wire::Surface)
    return;
  packet.rectangles.reserve(packet.bands.size());
  std::ranges::transform(packet.bands, std::back_inserter(packet.rectangles), [&](Band &band) {
    auto rectangle = BitmapRectangle({band.area, band.pixels}, wire == Wire::Planar);
    if (depth != 32) {
      auto stride = (unsigned(band.area.w) * (depth / 8) + 3) & ~3u;
      rectangle.bitsPerPixel = depth;
      rectangle.width = stride / (depth / 8);
      rectangle.cbScanWidth = stride;
      rectangle.cbUncompressedSize = band.pixels.size();
    }
    return rectangle;
  });
}
bool LegacyFrame::Write(rdpUpdate *update, Packet &packet) {
  Expects(!packet.bands.empty(), "encoded packet exists");
  switch (wire) {
  case Wire::Surface:
    return SendSurfaceBits(update, {packet.bands.front().area, packet.bands.front().pixels}, codec);
  case Wire::Bitmap:
  case Wire::Planar:
    return SendBitmapBand(update, packet.rectangles);
  default:
    utilities::Unreachable(wire);
  }
}
bool LegacyFrame::Send(Peer &peer) {
  Expects(peer.snapshot != nullptr, "immutable frame exists");
  if (!next) {
    if (!peer.Marker(SURFACECMD_FRAMEACTION_BEGIN))
      return false;
    next = 0;
  }
  while (*next < packets.size()) {
    if (peer.client->IsWriteBlocked(peer.client.get()))
      return true;
    if (!Write(peer.client->context->update, packets[*next]))
      return false;
    ++*next;
  }
  if (peer.client->IsWriteBlocked(peer.client.get())) return true;
  if (!peer.Marker(SURFACECMD_FRAMEACTION_END)) return false;
  std::scoped_lock lock(peer.owner.frame_guard);
  std::size_t bytes = 0;
  for (auto const& packet : packets) for (auto const& band : packet.bands) bytes += band.pixels.size();
  peer.FrameSent(bytes);
  peer.snapshot.reset();
  peer.sending.clear();
  packets.clear();
  return true;
}
} // namespace Backend
