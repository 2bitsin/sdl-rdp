#include <sdl-rdp/video/legacy-frame.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/update.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
namespace sdl_rdp::video::detail::legacy_frame {
using sdl_rdp::configuration::Codec;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::utilities::AreaBytes;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::video::EncodePlanarRows;

namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;
constexpr std::size_t BitmapHeaderReserve    = 1024;
constexpr int         RemoteFxBandRows       = 64;

auto SendSurfaceBits(rdpUpdate& update, Rect area, std::span<std::byte> payload, std::uint32_t codec) -> bool {
  Expects(update.SurfaceBits != nullptr, "surface callback exists");
  auto command = SURFACE_BITS_COMMAND{ };
  command.cmdType              = CMDTYPE_SET_SURFACE_BITS;
  command.skipCompression      = true;
  command.destLeft             = Narrowed<std::uint32_t>(area.x);
  command.destTop              = Narrowed<std::uint32_t>(area.y);
  command.destRight            = Narrowed<std::uint32_t>(area.x + area.w);
  command.destBottom           = Narrowed<std::uint32_t>(area.y + area.h);
  command.bmp.bpp              = 32u;
  command.bmp.codecID          = Narrowed<std::uint16_t>(codec);
  command.bmp.width            = Narrowed<std::uint16_t>(area.w);
  command.bmp.height           = Narrowed<std::uint16_t>(area.h);
  command.bmp.bitmapDataLength = Narrowed<std::uint32_t>(payload.size());
  command.bmp.bitmapData       = oxbox::utilities::SpanCast<std::uint8_t>(payload).data();
  return update.SurfaceBits(update.context, &command);
}

// Bitmap update corners are inclusive, unlike a surface command's.
auto BitmapArea(Rect area) -> BITMAP_DATA {
  auto rectangle = BITMAP_DATA{ };
  rectangle.destLeft           = Narrowed<std::uint32_t>(area.x);
  rectangle.destTop            = Narrowed<std::uint32_t>(area.y);
  rectangle.destRight          = Narrowed<std::uint32_t>(area.x + area.w - 1);
  rectangle.destBottom         = Narrowed<std::uint32_t>(area.y + area.h - 1);
  rectangle.width              = Narrowed<std::uint32_t>(area.w);
  rectangle.height             = Narrowed<std::uint32_t>(area.h);
  rectangle.cbScanWidth        = Narrowed<std::uint32_t>(RowBytes(area.w));
  rectangle.cbUncompressedSize = Narrowed<std::uint32_t>(AreaBytes(area));
  return rectangle;
}
auto BitmapRectangle(Rect area, std::span<std::byte> payload, bool compressed) -> BITMAP_DATA {
  Expects(!payload.empty(), "bitmap payload exists");
  auto rectangle = BitmapArea(area);
  rectangle.bitsPerPixel       = 32u;
  rectangle.bitmapLength       = Narrowed<std::uint32_t>(payload.size());
  rectangle.bitmapDataStream   = oxbox::utilities::SpanCast<std::uint8_t>(payload).data();
  rectangle.compressed         = compressed;
  rectangle.cbCompMainBodySize = Narrowed<std::uint32_t>(payload.size());
  return rectangle;
}
auto SendBitmapBand(rdpUpdate& update, std::span<BITMAP_DATA> rectangles) -> bool {
  Expects(update.BitmapUpdate != nullptr, "bitmap callback exists");
  Expects(!rectangles.empty(), "bitmap batch exists");
  auto batch = BITMAP_UPDATE{ };
  batch.number          = Narrowed<std::uint32_t>(rectangles.size());
  batch.rectangles      = rectangles.data();
  batch.skipCompression = true;
  return update.BitmapUpdate(update.context, &batch);
}

auto PackedStride(int width, std::uint32_t depth) -> std::uint32_t {
  return (Narrowed<std::uint32_t>(width) * (depth / 8) + 3) & ~3u;
}
auto Convert(PixelBand band, std::uint32_t depth) -> std::vector<std::byte> {
  Expects(std::ranges::contains(std::array{ 16u, 24u }, depth), "supported packed colour depth");
  auto const             area      = band.area;
  auto const             format    = depth == 16 ? PIXEL_FORMAT_RGB16 : PIXEL_FORMAT_BGR24;
  auto const             stride    = PackedStride(area.w, depth);
  std::vector<std::byte> converted(std::size_t{ stride } * Narrowed<std::size_t>(area.h));
  auto const             target    = oxbox::utilities::SpanCast<std::uint8_t>(std::span{ converted });
  auto const             width     = Narrowed<std::uint32_t>(area.w);
  auto const             height    = Narrowed<std::uint32_t>(area.h);
  auto const             source    = Narrowed<std::uint32_t>(RowBytes(area.w));
  if (!freerdp_image_copy(target.data(), format, stride, 0, 0, width, height, band.pixels.data(), PIXEL_FORMAT_BGRX32,
                          source, 0, 0, nullptr, FREERDP_FLIP_NONE))
    return { };
  return converted;
}
auto PacketBytes(auto const& packets) -> std::size_t {
  auto sizes = packets | std::views::transform([](auto const& packet) { return std::span(packet.bands); })
               | std::views::join | std::views::transform([](auto const& band) { return band.bytes.size(); });
  return std::ranges::fold_left(sizes, std::size_t{ 0 }, std::plus{ });
}
}
LegacyFrame::LegacyFrame(PeerLink& link, Configuration const& configuration, Activation& activation, PeerFrames& frames,
                         FramePacing& pacing, Encoder& encoder, Scaler& scaler) noexcept
    : _link{ link }, _configuration{ configuration }, _activation{ activation }, _frames{ frames }, _pacing{ pacing },
      _encoder{ encoder }, _scaler{ scaler } { }
auto LegacyFrame::SelectEncoder() -> bool {
  auto const previous = _encoder.SelectedCodec();
  if (!_encoder.Select(_link.Settings(), _configuration.CodecPreference())) return false;
  if (previous != _encoder.SelectedCodec()) _activation.CodecChanged(_encoder.SelectedCodec());
  return true;
}
auto LegacyFrame::Marker(std::uint16_t action) -> bool {
  if (!_link.Settings().Get(BoolKey::FrameMarkerCommandEnabled)) return true;
  auto&                      context = _link.Context();
  SURFACE_FRAME_MARKER const marker  { action, _pacing.Frame() };
  return context.update->SurfaceFrameMarker(&context, &marker);
}
auto LegacyFrame::Prepare() -> bool {
  ExpectCaptured(_frames);
  _queue.packets.clear();
  _queue.next.reset();
  if (!SelectEncoder()) return false;
  auto const settings = _link.Settings();
  auto const depth    = settings.Get(NumberKey::ColorDepth);
  auto const wire     = depth != 32 ? LegacyWire::Bitmap
                        : _encoder.SelectedCodec() == Codec::Planar ? LegacyWire::Planar
                        : settings.Get(BoolKey::SurfaceCommandsEnabled) ? LegacyWire::Surface
                                                                        : LegacyWire::Bitmap;
  _format = { .depth = depth, .codec = wire == LegacyWire::Surface ? _encoder.Id(settings) : 0, .wire = wire };
  return true;
}
auto LegacyFrame::AppendPlanar(Packet& packet, std::size_t& wire_size, Rect area, std::span<std::byte const> payload)
    -> void {
  auto size = payload.size();
  if (wire_size + 26 + size > BITMAP_RECTANGLE_LIMIT && !packet.bands.empty()) {
    _queue.packets.push_back(std::move(packet));
    packet    = { };
    wire_size = 4;
  }
  packet.bands.push_back({ area, { payload.begin(), payload.end() } });
  wire_size += 26 + size;
}
auto LegacyFrame::Planar(Rect area) -> bool {
  Expects(area.w > 0, "planar width exists");
  Expects(area.h > 0, "planar height exists");
  Packet      packet;
  std::size_t wire_size = 4;
  auto const  append    = [&](Rect row, std::span<std::byte const> payload) {
    AppendPlanar(packet, wire_size, row, payload);
    return true;
  };
  auto const  encoded   = EncodePlanarRows(_encoder, _scaler, area, append);
  if (!encoded) return false;
  if (!packet.bands.empty()) _queue.packets.push_back(std::move(packet));
  return true;
}
auto LegacyFrame::AppendBand(PixelBand band) -> bool {
  auto const area = band.area;
  if (_format.depth != 32) {
    auto converted = Convert(band, _format.depth);
    if (converted.empty()) return false;
    _queue.packets.push_back({ { Band{ .area = area, .bytes = std::move(converted) } }, { } });
    return true;
  }
  auto const raw = _encoder.SelectedCodec() == Codec::Raw;
  if (!raw && !_encoder.Encode(band.pixels, area.w, area.h)) return false;
  auto const payload = raw ? oxbox::utilities::AsBytes(band.pixels) : _encoder.Payload();
  _queue.packets.push_back({ { Band{ .area = area, .bytes = { payload.begin(), payload.end() } } }, { } });
  return true;
}
auto LegacyFrame::Bands(Rect area) -> bool {
  auto const order = _encoder.SelectedCodec() == Codec::Raw ? RowOrder::BottomUp : RowOrder::TopDown;
  auto const lines = _encoder.SelectedCodec() == Codec::RemoteFx
                         ? RemoteFxBandRows
                         : std::max(1,
                                    Narrowed<int>((BITMAP_RECTANGLE_LIMIT - BitmapHeaderReserve) / RowBytes(area.w)));
  for (int row = 0; row < area.h; row += lines) {
    Rect const band{ .x = area.x, .y = area.y + row, .w = area.w, .h = std::min(lines, area.h - row) };
    _scratch.resize(AreaBytes(band));
    if (!AppendBand(_scaler.Copy(band, _scratch, order))) return false;
  }
  return true;
}
auto LegacyFrame::Encode() -> bool {
  ExpectCaptured(_frames);
  auto encoded = std::ranges::all_of(
      _scaler.Areas(), [&](Rect area) { return _format.wire == LegacyWire::Planar ? Planar(area) : Bands(area); });
  if (encoded) std::ranges::for_each(_queue.packets, [&](auto& packet) { Describe(packet); });
  return encoded;
}
auto LegacyFrame::Describe(Packet& packet) const -> void {
  if (_format.wire == LegacyWire::Surface) return;
  auto const depth = _format.depth;
  packet.rectangles.reserve(packet.bands.size());
  std::ranges::transform(packet.bands, std::back_inserter(packet.rectangles), [&](Band& band) {
    auto rectangle = BitmapRectangle(band.area, band.bytes, _format.wire == LegacyWire::Planar);
    if (depth != 32) {
      auto stride = PackedStride(band.area.w, depth);
      rectangle.bitsPerPixel       = depth;
      rectangle.width              = stride / (depth / 8);
      rectangle.cbScanWidth        = stride;
      rectangle.cbUncompressedSize = Narrowed<std::uint32_t>(band.bytes.size());
    }
    return rectangle;
  });
}
auto LegacyFrame::Write(Packet& packet) -> bool {
  Expects(!packet.bands.empty(), "encoded packet exists");
  auto& update = *_link.Context().update;
  switch (_format.wire) {
  case LegacyWire::Surface:
    return SendSurfaceBits(update, packet.bands.front().area, packet.bands.front().bytes, _format.codec);
  case LegacyWire::Bitmap:
  case LegacyWire::Planar: return SendBitmapBand(update, packet.rectangles);
  default:                 Unreachable(_format.wire);
  }
}
auto LegacyFrame::Finish() -> bool {
  if (_link.WriteBlocked()) return true;
  if (!Marker(SURFACECMD_FRAMEACTION_END)) return false;
  _pacing.Sent(_frames,
               { .bytes = PacketBytes(_queue.packets), .encoded = _encoder.EncodeTime(), .avc = std::nullopt });
  _queue.packets.clear();
  return true;
}
auto LegacyFrame::Send() -> bool {
  ExpectCaptured(_frames);
  if (!_queue.next) {
    if (!Marker(SURFACECMD_FRAMEACTION_BEGIN)) return false;
    _queue.next = 0;
  }
  for (auto& index = *_queue.next; index < _queue.packets.size(); ++index) {
    if (_link.WriteBlocked()) return true;
    if (!Write(_queue.packets[index])) return false;
  }
  return _link.WriteBlocked() || Finish();
}
auto LegacyFrame::Delivered() const noexcept -> bool {
  return !_frames.Snapshot();
}
}
