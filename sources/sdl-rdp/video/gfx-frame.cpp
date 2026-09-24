#include "_detail/planar-rows.hpp"
#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/configuration.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/core/picture-geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame-pacing.hpp>
#include <sdl-rdp/video/gfx.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <oxbox/utilities/span.hpp>
#include <winpr/sysinfo.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <utility>

namespace Backend {
namespace {
using InitializedRegion = std::unique_ptr<REGION16, Releases<region16_uninit>>;
constexpr std::size_t   ProgressiveSyncBytes        = 12;
constexpr std::size_t   ProgressiveContextBytes     = 10;
constexpr std::size_t   ProgressiveBlockHeaderBytes = sizeof(std::uint16_t) + sizeof(std::uint32_t);
constexpr auto          ProgressiveHeaderBytes      = ProgressiveSyncBytes + ProgressiveContextBytes;
constexpr std::uint16_t ProgressiveSyncBlock        = 0xCCC0;
constexpr std::uint16_t ProgressiveContextBlock     = 0xCCC3;
constexpr std::size_t   WireToSurfaceHeaderBytes    = 25;
auto SurfaceCommand(sdlrdp_rect area, std::span<std::byte> data, std::uint32_t codec) -> RDPGFX_SURFACE_COMMAND {
  RDPGFX_SURFACE_COMMAND command{ };
  command.surfaceId = GraphicsSurfaceId;
  command.codecId   = codec;
  command.contextId = GraphicsContextId;
  command.format    = PIXEL_FORMAT_BGRX32;
  command.left      = Narrowed<std::uint32_t>(area.x);
  command.top       = Narrowed<std::uint32_t>(area.y);
  command.right     = Narrowed<std::uint32_t>(area.x + area.w);
  command.bottom    = Narrowed<std::uint32_t>(area.y + area.h);
  command.width     = Narrowed<std::uint32_t>(area.w);
  command.height    = Narrowed<std::uint32_t>(area.h);
  command.length    = Narrowed<std::uint32_t>(data.size());
  command.data      = oxbox::utilities::SpanCast<std::uint8_t>(data).data();
  return command;
}
auto Persistent(sdlrdp_codec codec) -> bool {
  switch (codec) {
  case SDLRDP_CODEC_PROGRESSIVE:
  case SDLRDP_CODEC_AVC420: return true;
  case SDLRDP_CODEC_AUTO:
  case SDLRDP_CODEC_PLANAR:
  case SDLRDP_CODEC_REMOTEFX:
  case SDLRDP_CODEC_NSCODEC:
  case SDLRDP_CODEC_RAW: return false;
  default:               utilities::Unreachable(codec);
  }
}
auto FellBack(sdlrdp_codec preference, sdlrdp_codec requested, sdlrdp_codec choice) -> bool {
  return preference == SDLRDP_CODEC_AVC420 && requested != preference && choice != preference;
}
auto ExpectSurface(bool confirmed, Extent surface) -> void {
  Expects(confirmed, "graphics capability confirmed");
  Expects(surface.width > 0, "surface width is positive");
  Expects(surface.height > 0, "surface height is positive");
}
auto EachArea(bool confirmed, Extent surface, Scaler const& scaler, std::predicate<sdlrdp_rect> auto send) -> bool {
  ExpectSurface(confirmed, surface);
  return std::ranges::all_of(scaler.Areas(), send);
}
auto SurfaceStride(Extent surface) -> std::uint32_t {
  return Avc::Aligned(surface.width) * std::uint32_t{ PixelBytes };
}
auto ExpectInside(sdlrdp_rect area, Extent surface) -> void {
  Expects(area.x >= 0, "command left edge is nonnegative");
  Expects(area.y >= 0, "command top edge is nonnegative");
  Expects(area.w > 0, "command width is positive");
  Expects(area.h > 0, "command height is positive");
  Expects(std::cmp_less_equal(area.x + area.w, surface.width), "command right edge fits surface");
  Expects(std::cmp_less_equal(area.y + area.h, surface.height), "command bottom edge fits surface");
}
constexpr auto BlockHeader(std::uint16_t block, std::uint8_t bytes)
    -> std::array<std::byte, ProgressiveBlockHeaderBytes> {
  // MS-RDPEGFX block headers are little-endian: the type's low byte, then its high byte.
  return { std::byte{ static_cast<std::uint8_t>(block) },
           std::byte{ static_cast<std::uint8_t>(block >> 8) },
           std::byte{ bytes },
           std::byte{ },
           std::byte{ },
           std::byte{ } };
}
// FreeRDP 3.32 rfx.c:2499 repeats SYNC/CONTEXT; GRD sends them once per surface context.
auto ProgressiveHeaders(std::span<std::byte const> data) -> bool {
  if (data.size() < ProgressiveHeaderBytes) return false;
  constexpr auto sync    = BlockHeader(ProgressiveSyncBlock, std::uint8_t{ ProgressiveSyncBytes });
  constexpr auto context = BlockHeader(ProgressiveContextBlock, std::uint8_t{ ProgressiveContextBytes });
  return std::ranges::equal(sync, data.first(sync.size()))
         && std::ranges::equal(context, data.subspan(ProgressiveSyncBytes, context.size()));
}
}
auto GfxChannel::CodecChoice() -> sdlrdp_codec {
  auto choice = _configuration.Codec();
  if (choice == SDLRDP_CODEC_AUTO) choice = SDLRDP_CODEC_AVC420;
  if (choice == SDLRDP_CODEC_AVC420 && !SelectAvc()) choice = SDLRDP_CODEC_PROGRESSIVE;
  if (choice != SDLRDP_CODEC_AVC420 && choice != SDLRDP_CODEC_RAW && choice != SDLRDP_CODEC_PLANAR)
    choice = SDLRDP_CODEC_PROGRESSIVE;
  return choice;
}
auto GfxChannel::AvcFailure() -> std::string {
  if (!Avc::Encoder::Available()) return Avc::Encoder::UnavailableReason();
  if (!_avc_allowed) return "confirmed capabilities do not allow AVC420";
  if (_avc.IsOpen()) return { };
  auto const   desktop = _sources.scaler.get().Target();
  Extent const size    { .width = Narrowed<std::uint32_t>(desktop.w), .height = Narrowed<std::uint32_t>(desktop.h) };
  auto const   opened  = _avc.Open(size, Avc::Bitrate(size, _configuration.AvcBitrate()), _avc_rate);
  return opened ? std::string{ } : _avc.Error();
}
auto GfxChannel::CompressProgressive(REGION16& damage, Stopwatch const& watch) -> bool {
  std::uint8_t* data    = nullptr;
  std::uint32_t size    = 0;
  auto          picture { Picture()               };
  auto          stride  { SurfaceStride(_surface) };
  auto          result  = progressive_compress(_progressive.get(), picture.data(), picture.size(), PIXEL_FORMAT_BGRX32,
                                               _surface.width, _surface.height, stride, &damage, &data, &size);
  _sources.encoder.get().Charge(watch.Elapsed());
  return result >= 0 && data && ProgressivePayload(oxbox::utilities::AsBytes(std::span{ data, size }));
}
auto GfxChannel::ProgressiveDamage(REGION16& damage) const -> bool {
  return std::ranges::all_of(_sources.scaler.get().Areas(), [&](sdlrdp_rect area) {
    RECTANGLE_16 const wire{ Narrowed<std::uint16_t>(area.x), Narrowed<std::uint16_t>(area.y),
                             Narrowed<std::uint16_t>(area.x + area.w), Narrowed<std::uint16_t>(area.y + area.h) };
    return region16_union_rect(&damage, &damage, &wire);
  });
}
auto GfxChannel::AvcTimes() const -> std::optional<Avc::EncodingTimes> {
  Expects(!_prepared.empty(), "accounting a prepared frame");
  if (_prepared.front().codec != RDPGFX_CODECID_AVC420) return std::nullopt;
  return _avc.Timing();
}
auto GfxChannel::FinishFrame() -> bool {
  auto const encoded = _sources.encoder.get().EncodeTime();
  _sources.pacing.get().Sent(_sources.frames.get(), { .bytes = _frame_bytes, .encoded = encoded, .avc = AvcTimes() });
  _last_bytes = _frame_bytes;
  _prepared.clear();
  return true;
}
auto GfxChannel::Select() -> bool {
  Expects(_confirmed, "codec follows capability confirmation");
  auto preference = _configuration.Codec();
  auto choice     = CodecChoice();
  auto previous   = _sources.encoder.get().Codec();
  if (choice == SDLRDP_CODEC_PLANAR && !_sources.encoder.get().SetupPlanar(&_link.Settings(), true)) return false;
  // Raw and planar do not populate the persistent progressive surface.
  if (previous != choice && Persistent(choice) && _sources.frames.get().Snapshot()) _sources.frames.get().Include();
  if (previous != choice) _force_idr = true;
  _sources.encoder.get().Use(choice);
  bool const changed = previous != choice || FellBack(preference, _requested, choice);
  _requested = preference;
  if (changed) _activation.CodecChanged(choice);
  return true;
}
auto GfxChannel::SelectAvc() -> bool {
  Expects(_confirmed, "codec follows capability confirmation");
  auto rate = _sources.pacing.get().Effective();
  if (_avc.IsOpen() && _avc_rate != rate) {
    _avc.Close();
    _force_idr = true;
  }
  _avc_rate = rate;
  bool const explicit_avc = _configuration.Codec() == SDLRDP_CODEC_AVC420;
  if (_avc_rejected && (!explicit_avc || _avc_logged)) return false;
  auto reason = AvcFailure();
  if (reason.empty()) return true;
  if (!_avc_logged && explicit_avc)
    _diagnostics.Log(SDLRDP_LOG_INFO, "AVC420 falls back to progressive: " + reason + ".");
  _avc_logged   |= explicit_avc;
  _avc_rejected =  true;
  return false;
}
auto GfxChannel::Picture() -> std::span<std::uint8_t const> {
  auto const& snapshot = _sources.frames.get().Snapshot();
  ExpectCaptured(_sources.frames.get());
  if (SameSize(snapshot.Bounds(), Whole(_surface))) return snapshot.Pixels();
  auto const pitch = std::size_t{ Avc::Aligned(_surface.width) } * PixelBytes;
  std::ranges::for_each(_sources.scaler.get().Areas(), [&](sdlrdp_rect area) {
    auto const offset = (Narrowed<std::size_t>(area.y) * pitch) + (Narrowed<std::size_t>(area.x) * PixelBytes);
    _sources.scaler.get().Place(area, std::span(_pixels).subspan(offset), pitch);
  });
  Avc::ReplicateEdges(_pixels, _surface);
  return _pixels;
}
auto GfxChannel::Avc420() -> bool {
  Expects(_confirmed, "graphics capability confirmed");
  Expects(_avc.IsOpen(), "AVC encoder is open");
  Stopwatch const watch;
  _regions.Clear();
  std::ranges::for_each(_sources.scaler.get().Areas(), [&](sdlrdp_rect area) { _regions.Add(area); });
  auto picture{ Picture()                                          };
  auto stride { SurfaceStride(_surface)                            };
  auto data   { _avc.Encode(picture, stride, _force_idr, _payload) };
  _sources.encoder.get().Charge(watch.Elapsed());
  if (data.empty()) return false;
  _force_idr   =  false;
  _frame_bytes += data.size() + WireToSurfaceHeaderBytes + _regions.Bytes();
  _prepared.push_back({ _regions.Bounds(), 0, data.size(), RDPGFX_CODECID_AVC420 });
  return true;
}
auto GfxChannel::Command(sdlrdp_rect area, std::span<std::byte const> data, std::uint32_t codec) -> bool {
  Expects(!data.empty(), "encoded graphics payload exists");
  _frame_bytes += data.size() + WireToSurfaceHeaderBytes;
  auto offset = _payload.size();
  _payload.insert(_payload.end(), data.begin(), data.end());
  _prepared.push_back({ area, offset, data.size(), codec });
  return true;
}
auto GfxChannel::WriteCommand(Packet const& packet) -> bool {
  Expects(_confirmed, "graphics capability confirmed");
  Expects(packet.length > 0, "graphics payload exists");
  ExpectInside(packet.area, _surface);
  auto const                  data    = std::span(_payload).subspan(packet.offset, packet.length);
  auto                        command = SurfaceCommand(packet.area, data, packet.codec);
  RDPGFX_AVC420_BITMAP_STREAM stream  { { Narrowed<std::uint32_t>(_regions.Areas().size()), _regions.Areas().data(),
                                          _regions.Quality().data() },
                                        Narrowed<std::uint32_t>(data.size()),
                                        oxbox::utilities::SpanCast<std::uint8_t>(data).data() };
  if (packet.codec == RDPGFX_CODECID_AVC420) command.extra = &stream;
  return Check(_context->SurfaceCommand(_context.get(), &command), "surface command");
}
auto GfxChannel::Progressive() -> bool {
  ExpectSurface(_confirmed, _surface);
  Stopwatch const watch;
  if (!_progressive) _progressive.reset(progressive_context_new_ex(true, THREADING_FLAGS_DISABLE_THREADS));
  if (!_progressive) return false;
  REGION16 damage;
  region16_init(&damage);
  InitializedRegion const owned{ &damage };
  return ProgressiveDamage(damage) && CompressProgressive(damage, watch);
}
auto GfxChannel::ProgressivePayload(std::span<std::byte const> data) -> bool {
  if (!ProgressiveHeaders(data)) return false;
  auto payload = data.subspan(_headers ? ProgressiveHeaderBytes : 0);
  if (!Command(Whole(_surface), payload, RDPGFX_CODECID_CAPROGRESSIVE)) return false;
  _headers = true;
  return true;
}
auto GfxChannel::Raw() -> bool {
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](sdlrdp_rect area) {
    _band.resize(AreaBytes(area));
    auto const band = _sources.scaler.get().Copy(area, _band, RowOrder::TopDown);
    return Command(area, oxbox::utilities::AsBytes(band.Pixels()), RDPGFX_CODECID_UNCOMPRESSED);
  });
}
auto GfxChannel::Planar() -> bool {
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](sdlrdp_rect area) {
    auto const command = [this](sdlrdp_rect row, std::span<std::byte const> payload) {
      return Command(row, payload, RDPGFX_CODECID_PLANAR);
    };
    return EncodePlanarRows(_sources.encoder.get(), _sources.scaler.get(), area, command);
  });
}
auto GfxChannel::Prepare() -> bool {
  ExpectCaptured(_sources.frames.get());
  Expects(_confirmed, "graphics capability confirmed");
  if (!_prepared.empty()) return true;
  return Surface() && Select();
}
auto GfxChannel::BeginPayload() -> void {
  constexpr std::size_t StartFrameBytes = 16;
  constexpr std::size_t EndFrameBytes   = 12;
  _frame_bytes = StartFrameBytes + EndFrameBytes;
  _payload.clear();
}
auto GfxChannel::Encode() -> bool {
  ExpectCaptured(_sources.frames.get());
  ExpectSurface(_confirmed, _surface);
  if (!_prepared.empty()) return true;
  BeginPayload();
  auto const codec = _sources.encoder.get().Codec();
  switch (codec) {
  case SDLRDP_CODEC_AVC420:      return Avc420();
  case SDLRDP_CODEC_PROGRESSIVE: return Progressive();
  case SDLRDP_CODEC_PLANAR:      return Planar();
  case SDLRDP_CODEC_RAW:         return Raw();
  default:                       utilities::Unreachable(codec);
  }
}
auto GfxChannel::Send() -> bool {
  ExpectCaptured(_sources.frames.get());
  Expects(_confirmed, "graphics capability confirmed");
  Expects(!_prepared.empty(), "frame is encoded before transport");
  if (!_sources.pacing.get().Admit([this] { return FrameWindow(); })) return true;
  SYSTEMTIME time;
  GetSystemTime(&time);
  auto const                   id    = _sources.pacing.get().Frame();
  RDPGFX_START_FRAME_PDU const start { FrameTimestamp(time), id };
  RDPGFX_END_FRAME_PDU const   end   { id                       };
  if (!Check(_context->StartFrame(_context.get(), &start), "start frame")) return false;
  if (!std::ranges::all_of(_prepared, [this](Packet const& packet) { return WriteCommand(packet); })) return false;
  if (!Check(_context->EndFrame(_context.get(), &end), "end frame")) return false;
  return FinishFrame();
}
}
