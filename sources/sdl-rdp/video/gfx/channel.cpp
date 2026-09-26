#include <sdl-rdp/video/gfx/channel.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
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

#include <freerdp/channels/wtsvc.h>
#include <oxbox/utilities/span.hpp>
#include <oxbox/utilities/text.hpp>
#include <winpr/sysinfo.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <numeric>
#include <ranges>
#include <utility>

namespace sdl_rdp::video::gfx::detail::channel {
using sdl_rdp::diagnostics::FailuresThrough;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::BindContext;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::picture::Aligned;
using sdl_rdp::picture::FrameBytes;
using sdl_rdp::utilities::AreaBytes;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::SameSize;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::utilities::Whole;
using sdl_rdp::video::avc::Bitrate;
using sdl_rdp::video::avc::ReplicateEdges;
using sdl_rdp::video::EncodePlanarRows;
using sdl_rdp::video::frame::AcknowledgementMode;

namespace {
constexpr int MaximumSurfaceDimension = 32766;
auto Held(RdpgfxServerContext const& context) -> GfxChannel& {
  return CallbackOwner<GfxChannel, &RdpgfxServerContext::custom>(context);
}
auto Mode(std::uint32_t queue_depth) -> AcknowledgementMode {
  return queue_depth == SUSPEND_FRAME_ACKNOWLEDGEMENT ? AcknowledgementMode::Suspended : AcknowledgementMode::Tracking;
}
auto FitsProtocol(Rect desktop) -> bool {
  return desktop.w <= MaximumSurfaceDimension && desktop.h <= MaximumSurfaceDimension;
}
constexpr OperationName GraphicsCapabilities   { "Graphics capabilities"          };
constexpr OperationName GraphicsAcknowledgement{ "Graphics frame acknowledgement" };
constexpr OperationName GraphicsQoe            { "Graphics QoE acknowledgement"   };
constexpr OperationName GraphicsAssignment     { "Graphics channel assignment"    };
using sdl_rdp::freerdp_facade::Handled;
}
class GfxChannel::Callbacks {
public:
  static auto Install(RdpgfxServerContext& server) -> void;
};
auto GfxChannel::Callbacks::Install(RdpgfxServerContext& server) -> void {
  constexpr auto failures = FailuresThrough<&GfxChannel::FailureSource>;
  constexpr auto failed   = ERROR_INTERNAL_ERROR;
  // abi: psRdpgfxServerCapsAdvertise, FrameAcknowledge, QoeFrameAcknowledge, UINT is uint32_t; ChannelIdAssigned
  server.CapsAdvertise       = Handled<Held, &GfxChannel::Caps, GraphicsCapabilities, failures, failed>;
  server.FrameAcknowledge    = Handled<Held, &GfxChannel::Ack, GraphicsAcknowledgement, failures, failed>;
  server.QoeFrameAcknowledge = Handled<Held, &GfxChannel::Qoe, GraphicsQoe, failures, failed>;
  server.ChannelIdAssigned   = Handled<Held, &GfxChannel::Assign, GraphicsAssignment, failures, false>;
}
GfxChannel::GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration,
                       Activation& activation, FrameSources sources, DynamicChannel& owner)
    : _link{ link }, _diagnostics{ diagnostics }, _configuration{ configuration }, _activation{ activation },
      _sources{ sources }, _context{ rdpgfx_server_context_new(link.Channels().get()) }, _owner{ owner } { }
GfxChannel::~GfxChannel() = default;
auto GfxChannel::Assign(std::uint32_t id) -> bool {
  _assignment.emplace(_link.Dynamic().Assign(id, _owner));
  return true;
}
auto GfxChannel::Open() -> bool {
  if (!_context) return false;
  BindContext(*_context, *this, _link.Context());
  Callbacks::Install(*_context);
  return _context->Initialize(_context.get(), true) && _context->Open(_context.get());
}
auto GfxChannel::Event() const -> WaitHandle {
  return WaitHandle::Lent<rdpgfx_server_get_event_handle>(*_context);
}
auto GfxChannel::Pump() -> bool {
  auto result = rdpgfx_server_handle_messages(_context.get());
  return result == ERROR_NO_DATA || Check(result, "receive");
}
auto GfxChannel::Confirmed() const noexcept -> bool {
  return _confirmed;
}
auto GfxChannel::Timing() const noexcept -> GraphicsTiming const& {
  return _timing;
}
auto GfxChannel::Check(std::uint32_t result, std::string_view operation) const -> bool {
  if (result == CHANNEL_RC_OK) return true;
  _diagnostics.Log(LogLevel::Error, std::format("GFX {} failed: {}.", operation, result));
  return false;
}
auto GfxChannel::LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const -> void {
  if (_logged) return;
  auto const sets = oxbox::utilities::Joined(advertised, " ", [](RDPGFX_CAPSET const& cap) {
    return std::format("version=0x{:08x} flags=0x{:08x};", cap.version, cap.flags);
  });
  _diagnostics.Log(LogLevel::Info, "GFX advertised sets: " + sets);
}
auto GfxChannel::ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted) -> std::uint32_t {
  auto const codec      = _sources.encoder.get().SelectedCodec();
  bool const announcing = _activation.Holding();
  _activation.Announce(codec, _sources.pacing.get().Effective());
  if (announcing && wanted && codec != Codec::Avc420) _activation.CodecChanged(codec);
  _sources.pacing.get().Acknowledgements(AcknowledgementMode::Restarted);
  _sources.frames.get().Refresh();
  if (!_logged)
    _diagnostics.Log(LogLevel::Info,
                     std::format("GFX confirmed version=0x{:08x} flags=0x{:08x}.", selected.version, selected.flags));
  _logged = true;
  return CHANNEL_RC_OK;
}
auto GfxChannel::ResetSurface() -> bool {
  RDPGFX_DELETE_ENCODING_CONTEXT_PDU const encoding{ GraphicsSurfaceId, GraphicsContextId };
  if (_headers && !Check(_context->DeleteEncodingContext(_context.get(), &encoding), "delete encoding context"))
    return false;
  RDPGFX_DELETE_SURFACE_PDU const remove{ GraphicsSurfaceId };
  if (_surface.width && !Check(_context->DeleteSurface(_context.get(), &remove), "delete surface")) return false;
  constexpr std::uint32_t                PrimaryMonitor = 1;
  constexpr std::uint32_t                MonitorCount   = 1;
  auto const                             desktop        = _sources.scaler.get().Target();
  MONITOR_DEF                            monitor        { 0, 0, desktop.w - 1, desktop.h - 1, PrimaryMonitor };
  RDPGFX_RESET_GRAPHICS_PDU const reset{ Narrowed<std::uint32_t>(desktop.w), Narrowed<std::uint32_t>(desktop.h),
                                         MonitorCount, &monitor };
  RDPGFX_CREATE_SURFACE_PDU const create{ GraphicsSurfaceId, Narrowed<std::uint16_t>(desktop.w),
                                          Narrowed<std::uint16_t>(desktop.h), GFX_PIXEL_FORMAT_XRGB_8888 };
  RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU const map            { GraphicsSurfaceId, 0, 0, 0                         };
  return Check(_context->ResetGraphics(_context.get(), &reset), "reset graphics")
         && Check(_context->CreateSurface(_context.get(), &create), "create surface")
         && Check(_context->MapSurfaceToOutput(_context.get(), &map), "map surface");
}
auto GfxChannel::Caps(RDPGFX_CAPS_ADVERTISE_PDU const& caps) -> std::uint32_t {
  auto advertised = std::span(caps.capsSets, caps.capsSetCount);
  LogCapabilities(advertised);
  bool const wanted   = _configuration.CodecPreference() == Codec::Avc420;
  auto       selected = SelectCapability(advertised, Encoder::Available());
  if (!selected.version) return ERROR_NOT_SUPPORTED;
  RDPGFX_CAPS_CONFIRM_PDU const confirm{ &selected };
  if (!Check(_context->CapsConfirm(_context.get(), &confirm), "confirm")) return ERROR_INTERNAL_ERROR;
  ConfirmedCapability(selected);
  if (!Select()) return ERROR_INTERNAL_ERROR;
  return ActivateCapabilities(selected, wanted);
}
auto GfxChannel::ResetAvc() -> void {
  _avc.Close();
  _force_idr    = true;
  _avc_rejected = false;
}
auto GfxChannel::ConfirmedCapability(RDPGFX_CAPSET const& cap) -> void {
  Expects(cap.version, "supported capabilities confirmed");
  _avc_allowed = AllowsAvc(cap);
  ResetAvc();
  _confirmed         = true;
  _timing.ready_time = Activation::Clock::now() - _activation.ActivatedAt();
  _surface           = { };
  _headers           = false;
  _prepared.clear();
}
auto GfxChannel::Ack(RDPGFX_FRAME_ACKNOWLEDGE_PDU const& ack) -> std::uint32_t {
  _sources.pacing.get().Accept(ack.frameId);
  _queue_depth = ack.queueDepth;
  _sources.pacing.get().Acknowledgements(Mode(ack.queueDepth));
  return CHANNEL_RC_OK;
}
auto GfxChannel::Qoe(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& ack) -> std::uint32_t {
  _timing.qoe = ack;
  return CHANNEL_RC_OK;
}
auto GfxChannel::FrameWindow() const -> std::size_t {
  Expects(_queue_depth != SUSPEND_FRAME_ACKNOWLEDGEMENT, "acknowledgements are enabled");
  // MS-RDPEGFX 2.2.2.13 reports bytes, not frames; reserve at most one slot for that backlog.
  return _queue_depth && _queue_depth >= _last_bytes ? AcknowledgedFrameWindow - 1 : AcknowledgedFrameWindow;
}
auto GfxChannel::Surface() -> bool {
  Expects(_confirmed, "surface follows capability confirmation");
  auto const desktop = _sources.scaler.get().Target();
  if (SameSize(Whole(_surface), desktop)) return true;
  if (!FitsProtocol(desktop)) {
    _diagnostics.Log(LogLevel::Error, "GFX desktop exceeds the 32766-pixel protocol limit.");
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
auto GfxChannel::FailureSource() const noexcept -> Diagnostics const& {
  return _diagnostics;
}
namespace {
using InitializedRegion = std::unique_ptr<REGION16, Releases<region16_uninit>>;
constexpr std::size_t   ProgressiveSyncBytes        = 12;
constexpr std::size_t   ProgressiveContextBytes     = 10;
constexpr std::size_t   ProgressiveBlockHeaderBytes = sizeof(std::uint16_t) + sizeof(std::uint32_t);
constexpr auto          ProgressiveHeaderBytes      = ProgressiveSyncBytes + ProgressiveContextBytes;
constexpr std::uint16_t ProgressiveSyncBlock        = 0xCCC0;
constexpr std::uint16_t ProgressiveContextBlock     = 0xCCC3;
constexpr std::size_t   WireToSurfaceHeaderBytes    = 25;
auto SurfaceCommand(Rect area, std::span<std::byte> data, std::uint32_t codec) -> RDPGFX_SURFACE_COMMAND {
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
  return Aligned(surface.width) * std::uint32_t{ PixelBytes };
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
  return std::ranges::all_of(_sources.scaler.get().Areas(), [&](Rect area) {
    RECTANGLE_16 const wire{ Narrowed<std::uint16_t>(area.x), Narrowed<std::uint16_t>(area.y),
                             Narrowed<std::uint16_t>(area.x + area.w), Narrowed<std::uint16_t>(area.y + area.h) };
    return region16_union_rect(&damage, &damage, &wire);
  });
}
auto GfxChannel::AvcTimes() const -> std::optional<EncodingTimes> {
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
  auto preference = _configuration.CodecPreference();
  auto choice     = CodecChoice();
  auto previous   = _sources.encoder.get().SelectedCodec();
  if (choice == Codec::Planar && !_sources.encoder.get().SetupPlanar(_link.Settings(), true)) return false;
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
  if (!_avc_logged && explicit_avc)
    _diagnostics.Log(LogLevel::Info, "AVC420 falls back to progressive: " + reason + ".");
  _avc_logged   |= explicit_avc;
  _avc_rejected =  true;
  return false;
}
auto GfxChannel::Picture() -> std::span<std::uint8_t const> {
  auto const& snapshot = _sources.frames.get().Snapshot();
  ExpectCaptured(_sources.frames.get());
  if (SameSize(snapshot.Bounds(), Whole(_surface))) return snapshot.Pixels();
  auto const pitch = std::size_t{ Aligned(_surface.width) } * PixelBytes;
  std::ranges::for_each(_sources.scaler.get().Areas(), [&](Rect area) {
    auto const offset = (Narrowed<std::size_t>(area.y) * pitch) + (Narrowed<std::size_t>(area.x) * PixelBytes);
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
  _prepared.push_back({ _regions.Bounds(), 0, data.size(), RDPGFX_CODECID_AVC420 });
  return true;
}
auto GfxChannel::Command(Rect area, std::span<std::byte const> data, std::uint32_t codec) -> bool {
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
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](Rect area) {
    _band.resize(AreaBytes(area));
    auto const band = _sources.scaler.get().Copy(area, _band, RowOrder::TopDown);
    return Command(area, oxbox::utilities::AsBytes(band.pixels), RDPGFX_CODECID_UNCOMPRESSED);
  });
}
auto GfxChannel::Planar() -> bool {
  return EachArea(_confirmed, _surface, _sources.scaler.get(), [&](Rect area) {
    auto const command = [this](Rect row, std::span<std::byte const> payload) {
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
