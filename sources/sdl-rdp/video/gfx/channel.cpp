#include <sdl-rdp/video/gfx/channel.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <oxbox/utilities/span.hpp>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <ranges>
#include <utility>

namespace sdl_rdp::video::gfx::detail::channel {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::GraphicsCommand;
using sdl_rdp::freerdp_facade::GraphicsMonitor;
using sdl_rdp::freerdp_facade::SurfaceSpec;
using sdl_rdp::picture::Aligned;
using sdl_rdp::picture::FrameBytes;
using sdl_rdp::utilities::AreaBytes;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::SameSize;
using sdl_rdp::utilities::SizeOf;
using sdl_rdp::utilities::Stride;
using sdl_rdp::utilities::Stopwatch;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::utilities::Whole;
using sdl_rdp::video::avc::Bitrate;
using sdl_rdp::video::avc::ReplicateEdges;
using sdl_rdp::video::EncodePlanarRows;
using sdl_rdp::video::frame::AcknowledgementMode;

namespace {
constexpr int MaximumSurfaceDimension = 32766;
auto Mode(FrameAck ack) -> AcknowledgementMode {
  return ack.suspended ? AcknowledgementMode::Suspended : AcknowledgementMode::Tracking;
}
auto FitsProtocol(Rect desktop) -> bool {
  return desktop.w <= MaximumSurfaceDimension && desktop.h <= MaximumSurfaceDimension;
}
}
GfxChannel::GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration,
                       Activation& activation, FrameSources sources, DynamicChannel& owner)
    : LoggedFailures{ diagnostics }, _link{ link }, _configuration{ configuration }, _activation{ activation },
      _sources{ sources }, _channel{ link.Channels(), link.Connection(), *this }, _avc{ diagnostics }, _owner{ owner } {
}
GfxChannel::~GfxChannel() = default;
auto GfxChannel::ChannelAssigned(std::uint32_t id) -> void {
  _assignment.emplace(_link.Dynamic().Assign(id, _owner));
}
auto GfxChannel::Open() -> bool {
  return _channel.Open();
}
auto GfxChannel::Event() const -> WaitHandle {
  return _channel.Handle();
}
auto GfxChannel::Pump() -> bool {
  return Sent(_channel.Pump(), "receive");
}
auto GfxChannel::Confirmed() const noexcept -> bool {
  return _confirmed;
}
auto GfxChannel::Timing() const noexcept -> GraphicsTiming const& {
  return _timing;
}
auto GfxChannel::Sent(bool sent, std::string_view operation) const -> bool {
  if (!sent) Logger().Log(LogLevel::Error, std::format("GFX {} failed.", operation));
  return sent;
}
auto GfxChannel::LogCapabilities(std::span<GfxCapability const> advertised) const -> void {
  if (_logged) return;
  auto const sets = oxbox::utilities::Joined(advertised, " ", [](GfxCapability cap) {
    return std::format("version=0x{:08x} flags=0x{:08x};", std::to_underlying(cap.version),
                       std::to_underlying(cap.flags));
  });
  Logger().Log(LogLevel::Info, "GFX advertised sets: " + sets);
  if (!Encoder::Available()) Logger().Log(LogLevel::Warn, "AVC420 unavailable: " + Encoder::UnavailableReason());
}
auto GfxChannel::ActivateCapabilities(GfxCapability selected, bool wanted) -> void {
  auto const codec      = _sources.encoder.get().SelectedCodec();
  bool const announcing = _activation.Holding();
  _activation.Announce(codec, _sources.pacing.get().Effective());
  if (announcing && wanted && codec != Codec::Avc420) _activation.CodecChanged(codec);
  _sources.pacing.get().Acknowledgements(AcknowledgementMode::Restarted);
  _sources.frames.get().Refresh();
  if (!_logged)
    Logger().Log(LogLevel::Info, std::format("GFX confirmed version=0x{:08x} flags=0x{:08x}.",
                                             std::to_underlying(selected.version), std::to_underlying(selected.flags)));
  _logged = true;
}
auto GfxChannel::ResetSurface() -> bool {
  if (_headers
      && !Sent(_channel.DeleteEncodingContext(GraphicsSurfaceId, GraphicsContextId), "delete encoding context"))
    return false;
  if (_surface.width && !Sent(_channel.DeleteSurface(GraphicsSurfaceId), "delete surface")) return false;
  auto const desktop  = SizeOf(_sources.scaler.get().Target());
  auto const monitors = std::array{ GraphicsMonitor{ .area = Whole(desktop), .primary = true } };
  return Sent(_channel.ResetGraphics(desktop, monitors), "reset graphics")
         && Sent(_channel.CreateSurface(SurfaceSpec{ .id = GraphicsSurfaceId, .size = desktop }), "create surface")
         && Sent(_channel.MapSurfaceToOutput(GraphicsSurfaceId), "map surface");
}
auto GfxChannel::CapsAdvertise(std::span<GfxCapability const> advertised) -> bool {
  LogCapabilities(advertised);
  bool const wanted   = _configuration.CodecPreference() == Codec::Avc420;
  auto const selected = SelectCapability(advertised, Encoder::Available());
  if (!selected || !Sent(_channel.CapsConfirm(*selected), "confirm")) return false;
  ConfirmedCapability(*selected);
  if (!Select()) return false;
  ActivateCapabilities(*selected, wanted);
  return true;
}
auto GfxChannel::ResetAvc() -> void {
  _avc.Close();
  _force_idr    = true;
  _avc_rejected = false;
}
auto GfxChannel::ConfirmedCapability(GfxCapability cap) -> void {
  _avc_allowed = AllowsAvc(cap);
  ResetAvc();
  _confirmed         = true;
  _timing.ready_time = Activation::Clock::now() - _activation.ActivatedAt();
  _surface           = { };
  _headers           = false;
  _prepared.clear();
}
auto GfxChannel::FrameAcknowledge(FrameAck ack) -> void {
  _sources.pacing.get().Accept(ack.frame);
  _acknowledged = ack;
  _sources.pacing.get().Acknowledgements(Mode(ack));
}
auto GfxChannel::QoeFrameAcknowledge(QoeAck ack) -> void {
  _timing.qoe = ack;
}
auto GfxChannel::FrameWindow() const -> std::size_t {
  Expects(!_acknowledged.suspended, "acknowledgements are enabled");
  auto const depth = _acknowledged.queue_depth;
  // MS-RDPEGFX 2.2.2.13 reports bytes, not frames; reserve at most one slot for that backlog.
  return depth && depth >= _last_bytes ? AcknowledgedFrameWindow - 1 : AcknowledgedFrameWindow;
}
auto GfxChannel::Surface() -> bool {
  Expects(_confirmed, "surface follows capability confirmation");
  auto const desktop = _sources.scaler.get().Target();
  if (SameSize(Whole(_surface), desktop)) return true;
  if (!FitsProtocol(desktop)) {
    Logger().Log(LogLevel::Error, "GFX desktop exceeds the 32766-pixel protocol limit.");
    return false;
  }
  if (!ResetSurface()) return false;
  _surface = { .width = Narrowed<std::uint32_t>(desktop.w), .height = Narrowed<std::uint32_t>(desktop.h) };
  _pixels.resize(FrameBytes(_surface));
  _headers = false;
  _progressive.reset();
  ResetAvc();
  _sources.frames.get().Resend();
  auto const matches = SameSize(Whole(_surface), desktop);
  Ensures(matches, "surface matches the desktop");
  return true;
}
namespace {
constexpr std::size_t   ProgressiveSyncBytes        = 12;
constexpr std::size_t   ProgressiveContextBytes     = 10;
constexpr std::size_t   ProgressiveBlockHeaderBytes = sizeof(std::uint16_t) + sizeof(std::uint32_t);
constexpr auto          ProgressiveHeaderBytes      = ProgressiveSyncBytes + ProgressiveContextBytes;
constexpr std::uint16_t ProgressiveSyncBlock        = 0xCCC0;
constexpr std::uint16_t ProgressiveContextBlock     = 0xCCC3;
constexpr std::size_t   WireToSurfaceHeaderBytes    = 25;
auto Persistent(Codec codec) -> bool {
  switch (codec) {
  case Codec::Progressive:
  case Codec::Avc420: return true;
  case Codec::Auto:
  case Codec::Planar:
  case Codec::RemoteFx:
  case Codec::NsCodec:
  case Codec::Raw: return false;
  default:         Unreachable(codec);
  }
}
auto FellBack(Codec preference, Codec requested, Codec choice) -> bool {
  return preference == Codec::Avc420 && requested != preference && choice != preference;
}
auto ExpectSurface(bool confirmed, Extent surface) -> void {
  Expects(confirmed, "graphics capability confirmed");
  Expects(surface.width > 0, "surface width is positive");
  Expects(surface.height > 0, "surface height is positive");
}
auto EachArea(bool confirmed, Extent surface, Scaler const& scaler, std::predicate<Rect> auto send) -> bool {
  ExpectSurface(confirmed, surface);
  return std::ranges::all_of(scaler.Areas(), send);
}
auto SurfaceStride(Extent surface) -> std::uint32_t {
  return Stride(Aligned(surface.width));
}
auto ExpectInside(Rect area, Extent surface) -> void {
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
auto GfxChannel::CodecChoice() -> Codec {
  auto choice = _configuration.CodecPreference();
  if (choice == Codec::Auto) choice = Codec::Avc420;
  if (choice == Codec::Avc420 && !SelectAvc()) choice = Codec::Progressive;
  if (choice != Codec::Avc420 && choice != Codec::Raw && choice != Codec::Planar) choice = Codec::Progressive;
  return choice;
}
auto GfxChannel::AvcFailure() -> std::string {
  if (!Encoder::Available()) return Encoder::UnavailableReason();
  if (!_avc_allowed) return "confirmed capabilities do not allow AVC420";
  if (_avc.IsOpen()) return { };
  auto const   desktop = _sources.scaler.get().Target();
  Extent const size    { .width = Narrowed<std::uint32_t>(desktop.w), .height = Narrowed<std::uint32_t>(desktop.h) };
  auto const   opened  = _avc.Open(size, Bitrate(size, _configuration.AvcBitrate()), _avc_rate);
  return opened ? std::string{ } : _avc.Error();
}
auto GfxChannel::AvcTimes() const -> std::optional<EncodingTimes> {
  Expects(!_prepared.empty(), "accounting a prepared frame");
  if (_prepared.front().codec != GfxCodec::Avc420) return std::nullopt;
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
  auto preference = _configuration.CodecPreference();
  auto choice     = CodecChoice();
  auto previous   = _sources.encoder.get().SelectedCodec();
  if (choice == Codec::Planar) _sources.encoder.get().SetupPlanar(_link.Connection().Settings(), true);
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
  bool const explicit_avc = _configuration.CodecPreference() == Codec::Avc420;
  if (_avc_rejected && (!explicit_avc || _avc_logged)) return false;
  auto reason = AvcFailure();
  if (reason.empty()) return true;
  if (!_avc_logged && explicit_avc) Logger().Log(LogLevel::Info, "AVC420 falls back to progressive: " + reason + ".");
  _avc_logged   |= explicit_avc;
  _avc_rejected =  true;
  return false;
}
auto GfxChannel::Picture() -> std::span<std::uint8_t const> {
  auto const& snapshot = _sources.frames.get().Snapshot();
  ExpectCaptured(_sources.frames.get());
  if (SameSize(snapshot.Bounds(), Whole(_surface))) return snapshot.Pixels();
  auto const pitch = std::size_t{ SurfaceStride(_surface) };
  std::ranges::for_each(_sources.scaler.get().Areas(), [&](Rect area) {
    auto const offset = (Narrowed<std::size_t>(area.y) * pitch) + RowBytes(area.x);
    _sources.scaler.get().Place(area, std::span(_pixels).subspan(offset), pitch);
  });
  ReplicateEdges(_pixels, _surface);
  return _pixels;
}
auto GfxChannel::Avc420() -> bool {
  Expects(_confirmed, "graphics capability confirmed");
  Expects(_avc.IsOpen(), "AVC encoder is open");
  Stopwatch const watch;
  _regions.Clear();
  std::ranges::for_each(_sources.scaler.get().Areas(), [&](Rect area) { _regions.Add(area); });
  auto picture{ Picture()                                          };
  auto stride { SurfaceStride(_surface)                            };
  auto data   { _avc.Encode(picture, stride, _force_idr, _payload) };
  _sources.encoder.get().Charge(watch.Elapsed());
  if (data.empty()) return false;
  _force_idr   =  false;
  _frame_bytes += data.size() + WireToSurfaceHeaderBytes + _regions.Bytes();
  _prepared.push_back({ _regions.Bounds(), 0, data.size(), GfxCodec::Avc420 });
  return true;
}
auto GfxChannel::Command(Rect area, std::span<std::byte const> data, GfxCodec codec) -> bool {
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
  GraphicsCommand command{ .surface = GraphicsSurfaceId,
                           .context = GraphicsContextId,
                           .codec   = packet.codec,
                           .area    = packet.area,
                           .payload = std::span<std::byte const>(_payload).subspan(packet.offset, packet.length) };
  if (packet.codec == GfxCodec::Avc420) command.metablock = _regions.Metablock();
  return Sent(_channel.SurfaceCommand(command), "surface command");
}
auto GfxChannel::Progressive() -> bool {
  ExpectSurface(_confirmed, _surface);
  Stopwatch const watch;
  if (!_progressive) _progressive.emplace();
  auto const damage  = _sources.scaler.get().Areas();
  auto const encoded = _progressive->Compress(Picture(), SurfaceStride(_surface), _surface, damage);
  _sources.encoder.get().Charge(watch.Elapsed());
  return encoded && ProgressivePayload(*encoded);
}
auto GfxChannel::ProgressivePayload(std::span<std::byte const> data) -> bool {
  if (!ProgressiveHeaders(data)) return false;
  auto payload = data.subspan(_headers ? ProgressiveHeaderBytes : 0);
  if (!Command(Whole(_surface), payload, GfxCodec::Progressive)) return false;
  _headers = true;
  return true;
}
auto GfxChannel::Raw() -> bool {
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](Rect area) {
    _band.resize(AreaBytes(area));
    auto const band = _sources.scaler.get().Copy(area, _band, RowOrder::TopDown);
    return Command(area, oxbox::utilities::AsBytes(band.pixels), GfxCodec::Uncompressed);
  });
}
auto GfxChannel::Planar() -> bool {
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](Rect area) {
    auto const command = [this](Rect row, std::span<std::byte const> payload) {
      return Command(row, payload, GfxCodec::Planar);
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
  auto const codec = _sources.encoder.get().SelectedCodec();
  switch (codec) {
  case Codec::Avc420:      return Avc420();
  case Codec::Progressive: return Progressive();
  case Codec::Planar:      return Planar();
  case Codec::Raw:         return Raw();
  default:                 Unreachable(codec);
  }
}
auto GfxChannel::Send() -> bool {
  ExpectCaptured(_sources.frames.get());
  Expects(_confirmed, "graphics capability confirmed");
  Expects(!_prepared.empty(), "frame is encoded before transport");
  if (!_sources.pacing.get().Admit([this] { return FrameWindow(); })) return true;
  auto const id = _sources.pacing.get().Frame();
  if (!Sent(_channel.StartFrame(id, std::chrono::system_clock::now()), "start frame")) return false;
  if (!std::ranges::all_of(_prepared, [this](Packet const& packet) { return WriteCommand(packet); })) return false;
  if (!Sent(_channel.EndFrame(id), "end frame")) return false;
  return FinishFrame();
}
}
