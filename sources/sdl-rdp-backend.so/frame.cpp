#include "_detail/legacy-frame.hpp"

#include "_detail/activation.hpp"
#include "_detail/configuration.hpp"
#include "_detail/encoder.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/planar-rows.hpp"
#include "_detail/rect.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <freerdp/codec/color.h>
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <span>
namespace Backend {
namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;
constexpr std::size_t BitmapHeaderReserve    = 1024;
constexpr int         RemoteFxBandRows       = 64;

auto SendSurfaceBits(rdpUpdate* update, PixelBand const& what, unsigned codec) -> bool {
  Expects(update != nullptr, "update exists");
  Expects(update->SurfaceBits != nullptr, "surface callback exists");
  auto const payload = what.Pixels();
  auto const area    = what.Area();
  auto       command = SURFACE_BITS_COMMAND{ };
  command.cmdType              = CMDTYPE_SET_SURFACE_BITS;
  command.skipCompression      = TRUE;
  command.destLeft             = area.x;
  command.destTop              = area.y;
  command.destRight            = (area.x + area.w);
  command.destBottom           = (area.y + area.h);
  command.bmp.bpp              = 32u;
  command.bmp.codecID          = codec;
  command.bmp.width            = static_cast<UINT16>(area.w);
  command.bmp.height           = static_cast<UINT16>(area.h);
  command.bmp.bitmapDataLength = static_cast<UINT32>(payload.size());
  command.bmp.bitmapData       = payload.data();
  return update->SurfaceBits(update->context, &command);
}

BITMAP_DATA BitmapArea(sdlrdp_rect area) {
  auto rectangle = BITMAP_DATA{ };
  rectangle.destLeft           = area.x;
  rectangle.destTop            = area.y;
  // Bitmap update corners are inclusive, unlike a surface command's.
  rectangle.destRight          = (area.x + area.w) - 1u;
  rectangle.destBottom         = (area.y + area.h) - 1u;
  rectangle.width              = area.w;
  rectangle.height             = area.h;
  rectangle.cbScanWidth        = area.w * PixelBytes;
  rectangle.cbUncompressedSize = std::size_t(area.w) * area.h * PixelBytes;
  return rectangle;
}
BITMAP_DATA BitmapRectangle(PixelBand const& band, bool compressed) {
  Expects(!band.Pixels().empty(), "bitmap payload exists");
  auto const payload   = band.Pixels();
  auto       rectangle = BitmapArea(band.Area());
  rectangle.bitsPerPixel       = 32u;
  rectangle.bitmapLength       = static_cast<UINT32>(payload.size());
  rectangle.bitmapDataStream   = payload.data();
  rectangle.compressed         = compressed;
  rectangle.cbCompMainBodySize = payload.size();
  return rectangle;
}
bool SendBitmapBand(rdpUpdate* update, std::span<BITMAP_DATA> rectangles) {
  Expects(update != nullptr, "update exists");
  Expects(update->BitmapUpdate != nullptr, "bitmap callback exists");
  Expects(!rectangles.empty(), "bitmap batch exists");
  auto batch = BITMAP_UPDATE{ };
  batch.number          = rectangles.size();
  batch.rectangles      = rectangles.data();
  batch.skipCompression = TRUE;
  return update->BitmapUpdate(update->context, &batch);
}

unsigned PackedStride(int width, unsigned depth) {
  return (unsigned(width) * (depth / 8) + 3) & ~3u;
}
auto Convert(PixelBand band, unsigned depth) -> std::vector<BYTE> {
  Expects(std::ranges::contains(std::array{ 16u, 24u }, depth), "supported packed colour depth");
  auto const        area      = band.Area();
  auto const        format    = depth == 16 ? PIXEL_FORMAT_RGB16 : PIXEL_FORMAT_BGR24;
  auto const        stride    = PackedStride(area.w, depth);
  std::vector<BYTE> converted(std::size_t(stride) * area.h);
  if (!freerdp_image_copy(converted.data(), format, stride, 0, 0, area.w, area.h, band.Pixels().data(),
                          PIXEL_FORMAT_BGRX32, area.w * PixelBytes, 0, 0, nullptr, FREERDP_FLIP_NONE))
    return { };
  return converted;
}
std::size_t PacketBytes(auto const& packets) {
  auto sizes = packets | std::views::transform([](auto const& packet) { return std::span(packet.bands); }) |
               std::views::join | std::views::transform([](auto const& band) { return band.pixels.size(); });
  return std::ranges::fold_left(sizes, std::size_t{ 0 }, std::plus{ });
}
} // namespace
LegacyFrame::LegacyFrame(PeerLink& link, Configuration const& configuration, Activation& activation,
                         PeerFrames& frames, FramePacing& pacing, Encoder& encoder, Scaler& scaler) noexcept
    : _link { link }, _configuration{ configuration }, _activation{ activation }, _frames{ frames }, _pacing{ pacing },
      _encoder{ encoder }, _scaler{ scaler } { }
bool LegacyFrame::SelectEncoder() {
  auto const previous = _encoder.Codec();
  if (!_encoder.Select(&_link.Settings(), _configuration.Codec())) return false;
  if (previous != _encoder.Codec()) _activation.CodecChanged(_encoder.Codec());
  return true;
}
bool LegacyFrame::Marker(UINT16 action) {
  auto& context = _link.Context();
  if (!freerdp_settings_get_bool(context.settings, FreeRDP_FrameMarkerCommandEnabled)) return true;
  SURFACE_FRAME_MARKER const marker{ action, _pacing.Frame() };
  return context.update->SurfaceFrameMarker(&context, &marker);
}
bool LegacyFrame::Prepare() {
  ExpectCaptured(_frames);
  _packets.clear();
  _next.reset();
  if (!SelectEncoder()) return false;
  auto const& settings = _link.Settings();
  auto const  depth    = freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth);
  auto const  wire     = depth != 32 ? LegacyWire::Bitmap
                         : _encoder.Codec() == SDLRDP_CODEC_PLANAR ? LegacyWire::Planar
                         : freerdp_settings_get_bool(&settings, FreeRDP_SurfaceCommandsEnabled) ? LegacyWire::Surface
                                                                                                : LegacyWire::Bitmap;
  _format = { .depth = depth, .codec = wire == LegacyWire::Surface ? _encoder.Id(&settings) : 0, .wire = wire };
  return true;
}
void LegacyFrame::AppendPlanar(Packet& packet, std::size_t& wire_size, sdlrdp_rect area,
                               std::span<BYTE const> payload) {
  auto size = payload.size();
  if (wire_size + 26 + size > BITMAP_RECTANGLE_LIMIT && !packet.bands.empty()) {
    _packets.push_back(std::move(packet));
    packet    = { };
    wire_size = 4;
  }
  packet.bands.push_back({ area, { payload.begin(), payload.end() } });
  wire_size += 26 + size;
}
bool LegacyFrame::Planar(sdlrdp_rect area) {
  Expects(area.w > 0, "planar width exists");
  Expects(area.h > 0, "planar height exists");
  Packet      packet;
  std::size_t wire_size = 4;
  auto const  append    = [&](sdlrdp_rect row, std::span<BYTE const> payload) {
    AppendPlanar(packet, wire_size, row, payload);
    return true;
  };
  auto const  encoded   = EncodePlanarRows(_encoder, _scaler, area, append);
  if (!encoded) return false;
  if (!packet.bands.empty()) _packets.push_back(std::move(packet));
  return true;
}
bool LegacyFrame::AppendBand(PixelBand band) {
  auto const area = band.Area();
  if (_format.depth != 32) {
    auto converted = Convert(band, _format.depth);
    if (converted.empty()) return false;
    _packets.push_back({ { Band{ .area = area, .pixels = std::move(converted) } }, { } });
    return true;
  }
  auto const raw = _encoder.Codec() == SDLRDP_CODEC_RAW;
  if (!raw && !_encoder.Encode(band.Pixels(), area.w, area.h)) return false;
  auto const payload = raw ? std::span<BYTE const>(band.Pixels()) : _encoder.Payload();
  _packets.push_back({ { Band{ .area = area, .pixels = { payload.begin(), payload.end() } } }, { } });
  return true;
}
bool LegacyFrame::Bands(sdlrdp_rect area) {
  auto const order = _encoder.Codec() == SDLRDP_CODEC_RAW ? RowOrder::BottomUp : RowOrder::TopDown;
  auto const lines = _encoder.Codec() == SDLRDP_CODEC_REMOTEFX
                         ? RemoteFxBandRows
                         : std::max(1, int((BITMAP_RECTANGLE_LIMIT - BitmapHeaderReserve) /
                                           (std::size_t(area.w) * PixelBytes)));
  for (int row = 0; row < area.h; row += lines) {
    auto const height  = std::min(lines, area.h - row);
    auto const scratch = _encoder.Scratch(std::size_t(area.w) * height * PixelBytes);
    if (!AppendBand(_scaler.Copy({ area.x, area.y + row, area.w, height }, scratch, order))) return false;
  }
  return true;
}
bool LegacyFrame::Encode() {
  ExpectCaptured(_frames);
  auto encoded = std::ranges::all_of(_scaler.Areas(), [&](sdlrdp_rect area) {
    return _format.wire == LegacyWire::Planar ? Planar(area) : Bands(area);
  });
  if (encoded) std::ranges::for_each(_packets, [&](auto& packet) { Describe(packet); });
  return encoded;
}
void LegacyFrame::Describe(Packet& packet) const {
  if (_format.wire == LegacyWire::Surface) return;
  auto const depth = _format.depth;
  packet.rectangles.reserve(packet.bands.size());
  std::ranges::transform(packet.bands, std::back_inserter(packet.rectangles), [&](Band& band) {
    auto rectangle = BitmapRectangle(PixelBand{ band.area, band.pixels }, _format.wire == LegacyWire::Planar);
    if (depth != 32) {
      auto stride = PackedStride(band.area.w, depth);
      rectangle.bitsPerPixel       = depth;
      rectangle.width              = stride / (depth / 8);
      rectangle.cbScanWidth        = stride;
      rectangle.cbUncompressedSize = band.pixels.size();
    }
    return rectangle;
  });
}
bool LegacyFrame::Write(Packet& packet) {
  Expects(!packet.bands.empty(), "encoded packet exists");
  auto* const update = _link.Context().update;
  switch (_format.wire) {
  case LegacyWire::Surface:
    return SendSurfaceBits(update, PixelBand{ packet.bands.front().area, packet.bands.front().pixels }, _format.codec);
  case LegacyWire::Bitmap:
  case LegacyWire::Planar:
    return SendBitmapBand(update, packet.rectangles);
  default:
    utilities::Unreachable(_format.wire);
  }
}
bool LegacyFrame::Finish() {
  if (_link.WriteBlocked()) return true;
  if (!Marker(SURFACECMD_FRAMEACTION_END)) return false;
  _pacing.Sent(_frames, { .bytes = PacketBytes(_packets), .encoded = _encoder.EncodeTime(), .avc = std::nullopt });
  _packets.clear();
  return true;
}
bool LegacyFrame::Send() {
  ExpectCaptured(_frames);
  if (!_next) {
    if (!Marker(SURFACECMD_FRAMEACTION_BEGIN)) return false;
    _next = 0;
  }
  for (auto& index = *_next; index < _packets.size(); ++index) {
    if (_link.WriteBlocked()) return true;
    if (!Write(_packets[index])) return false;
  }
  return _link.WriteBlocked() || Finish();
}
bool LegacyFrame::Delivered() const noexcept {
  return !_frames.Snapshot();
}
} // namespace Backend
