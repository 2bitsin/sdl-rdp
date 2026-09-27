#include <sdl-rdp/video/legacy-frame.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/updates.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

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
using sdl_rdp::freerdp_facade::ConvertPixels;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::PixelFormat;
using sdl_rdp::freerdp_facade::Updates;
using sdl_rdp::utilities::AreaBytes;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::SizeOf;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::video::EncodePlanarRows;

namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;
constexpr std::size_t BitmapHeaderReserve    = 1024;
constexpr int         RemoteFxBandRows       = 64;

auto Convert(PixelBand band, std::uint32_t depth) -> std::vector<std::byte> {
  Expects(std::ranges::contains(std::array{ 16u, 24u }, depth), "supported packed colour depth");
  return ConvertPixels(band.pixels, SizeOf(band.area), depth == 16 ? PixelFormat::Rgb16 : PixelFormat::Bgr24);
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
auto LegacyFrame::SelectEncoder() -> void {
  auto const previous = _encoder.SelectedCodec();
  _encoder.Select(_link.Connection().Settings(), _configuration.CodecPreference());
  if (previous != _encoder.SelectedCodec()) _activation.CodecChanged(_encoder.SelectedCodec());
}
auto LegacyFrame::Marker(FrameAction action) -> bool {
  if (!_link.Connection().Settings().Get(BoolKey::FrameMarkerCommandEnabled)) return true;
  return Updates{ _link.Connection() }.FrameMarker(action, _pacing.Frame());
}
auto LegacyFrame::Prepare() -> void {
  ExpectCaptured(_frames);
  _queue.packets.clear();
  _queue.next.reset();
  SelectEncoder();
  auto const settings = _link.Connection().Settings();
  auto const depth    = settings.Get(NumberKey::ColorDepth);
  auto const wire     = depth != 32 ? LegacyWire::Bitmap
                        : _encoder.SelectedCodec() == Codec::Planar ? LegacyWire::Planar
                        : settings.Get(BoolKey::SurfaceCommandsEnabled) ? LegacyWire::Surface
                                                                        : LegacyWire::Bitmap;
  _format = { .depth = depth, .codec = wire == LegacyWire::Surface ? _encoder.Id(settings) : 0, .wire = wire };
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
  packet.rectangles.reserve(packet.bands.size());
  std::ranges::transform(packet.bands, std::back_inserter(packet.rectangles), [&](Band& band) {
    return Bitmap{
      .area = band.area, .payload = band.bytes, .depth = _format.depth, .compressed = _format.wire == LegacyWire::Planar
    };
  });
}
auto LegacyFrame::Write(Packet& packet) -> bool {
  Expects(!packet.bands.empty(), "encoded packet exists");
  Updates updates{ _link.Connection() };
  switch (_format.wire) {
  case LegacyWire::Surface:
    return updates.SurfaceBits(
        { .area = packet.bands.front().area, .codec_id = _format.codec, .payload = packet.bands.front().bytes });
  case LegacyWire::Bitmap:
  case LegacyWire::Planar: return updates.Bitmaps(packet.rectangles);
  default:                 Unreachable(_format.wire);
  }
}
auto LegacyFrame::Finish() -> bool {
  if (_link.Connection().WriteBlocked()) return true;
  if (!Marker(FrameAction::End)) return false;
  _pacing.Sent(_frames,
               { .bytes = PacketBytes(_queue.packets), .encoded = _encoder.EncodeTime(), .avc = std::nullopt });
  _queue.packets.clear();
  return true;
}
auto LegacyFrame::Send() -> bool {
  ExpectCaptured(_frames);
  if (!_queue.next) {
    if (!Marker(FrameAction::Begin)) return false;
    _queue.next = 0;
  }
  for (auto& index = *_queue.next; index < _queue.packets.size(); ++index) {
    if (_link.Connection().WriteBlocked()) return true;
    if (!Write(_queue.packets[index])) return false;
  }
  return _link.Connection().WriteBlocked() || Finish();
}
auto LegacyFrame::Delivered() const noexcept -> bool {
  return !_frames.Snapshot();
}
}
