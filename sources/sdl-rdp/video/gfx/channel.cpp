#include <sdl-rdp/video/gfx/channel.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/dispatched.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <freerdp/channels/wtsvc.h>
#include <oxbox/utilities/text.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <utility>

namespace Backend {
namespace {
constexpr int MaximumSurfaceDimension = 32766;
auto Held(RdpgfxServerContext* context) -> GfxChannel& {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<GfxChannel>(context->custom);
}
auto Mode(std::uint32_t queue_depth) -> AcknowledgementMode {
  return queue_depth == SUSPEND_FRAME_ACKNOWLEDGEMENT ? AcknowledgementMode::Suspended : AcknowledgementMode::Tracking;
}
auto FitsProtocol(sdlrdp_rect desktop) -> bool {
  return desktop.w <= MaximumSurfaceDimension && desktop.h <= MaximumSurfaceDimension;
}
}
class GfxChannel::Callbacks {
public:
  static auto Install(RdpgfxServerContext& server) -> void;

private:
  template <auto HANDLER, class PduTy>
  static auto Handled(RdpgfxServerContext* context, PduTy const* pdu, OperationName operation) noexcept
      -> std::uint32_t;
};
template <auto HANDLER, class PduTy>
auto GfxChannel::Callbacks::Handled(RdpgfxServerContext* context, PduTy const* pdu, OperationName operation) noexcept
    -> std::uint32_t {
  auto& owner = Held(context);
  return Dispatched<HANDLER>(ERROR_INTERNAL_ERROR, owner, pdu, FailureLog{ owner._diagnostics, operation });
}
auto GfxChannel::Callbacks::Install(RdpgfxServerContext& server) -> void {
  // abi: psRdpgfxServerCapsAdvertise, UINT is uint32_t
  server.CapsAdvertise = [](RdpgfxServerContext* context,
                            RDPGFX_CAPS_ADVERTISE_PDU const* caps) noexcept -> std::uint32_t {
    return Handled<&GfxChannel::Caps>(context, caps, "Graphics capabilities");
  };
  // abi: psRdpgfxServerFrameAcknowledge, UINT is uint32_t
  server.FrameAcknowledge = [](RdpgfxServerContext* context,
                               RDPGFX_FRAME_ACKNOWLEDGE_PDU const* ack) noexcept -> std::uint32_t {
    return Handled<&GfxChannel::Ack>(context, ack, "Graphics frame acknowledgement");
  };
  // abi: psRdpgfxServerQoeFrameAcknowledge, UINT is uint32_t
  server.QoeFrameAcknowledge = [](RdpgfxServerContext* context,
                                  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* ack) noexcept -> std::uint32_t {
    return Handled<&GfxChannel::Qoe>(context, ack, "Graphics QoE acknowledgement");
  };
}
GfxChannel::GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration,
                       Activation& activation, FrameSources sources, DynamicChannel& owner)
    : _link{ link }, _diagnostics{ diagnostics }, _configuration{ configuration }, _activation{ activation },
      _sources{ sources }, _context{ rdpgfx_server_context_new(link.Channels()) }, _slot{ link.Dynamic(), owner } { }
GfxChannel::~GfxChannel() = default;
auto GfxChannel::Open() -> bool {
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  // abi: psRdpgfxServerChannelIdAssigned, BOOL is int
  _context->ChannelIdAssigned = [](RdpgfxServerContext* assigned, std::uint32_t id) noexcept -> int {
    auto& owner = Held(assigned);
    return owner._slot.Assigned(id, FailureLog{ owner._diagnostics, "Graphics channel assignment" });
  };
  Callbacks::Install(*_context);
  return _context->Initialize(_context.get(), true) && _context->Open(_context.get());
}
auto GfxChannel::Event() const -> WaitHandle {
  return rdpgfx_server_get_event_handle(_context.get());
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
auto GfxChannel::Check(std::uint32_t result, char const* operation) const -> bool {
  if (result == CHANNEL_RC_OK) return true;
  _diagnostics.Log(SDLRDP_LOG_ERROR, std::format("GFX {} failed: {}.", operation, result));
  return false;
}
auto GfxChannel::LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const -> void {
  if (_logged) return;
  auto const sets = oxbox::utilities::Joined(advertised, " ", [](RDPGFX_CAPSET const& cap) {
    return std::format("version=0x{:08x} flags=0x{:08x};", cap.version, cap.flags);
  });
  _diagnostics.Log(SDLRDP_LOG_INFO, "GFX advertised sets: " + sets);
}
auto GfxChannel::ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted) -> std::uint32_t {
  auto const codec      = _sources.encoder.get().Codec();
  bool const announcing = _activation.Holding();
  _activation.Announce(codec, _sources.pacing.get().Effective());
  if (announcing && wanted && codec != SDLRDP_CODEC_AVC420) _activation.CodecChanged(codec);
  _sources.pacing.get().Acknowledgements(AcknowledgementMode::Restarted);
  _sources.frames.get().Refresh();
  if (!_logged)
    _diagnostics.Log(SDLRDP_LOG_INFO,
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
  bool const wanted   = _configuration.Codec() == SDLRDP_CODEC_AVC420;
  auto       selected = SelectCapability(advertised, Avc::Encoder::Available());
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
  _confirmed = true;
  _timing.Ready(Activation::Clock::now() - _activation.ActivatedAt());
  _surface = { };
  _headers = false;
  _prepared.clear();
}
auto GfxChannel::Ack(RDPGFX_FRAME_ACKNOWLEDGE_PDU const& ack) -> std::uint32_t {
  _sources.pacing.get().Accept(ack.frameId);
  _queue_depth = ack.queueDepth;
  _sources.pacing.get().Acknowledgements(Mode(ack.queueDepth));
  return CHANNEL_RC_OK;
}
auto GfxChannel::Qoe(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& ack) -> std::uint32_t {
  _timing.Record(ack);
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
    _diagnostics.Log(SDLRDP_LOG_ERROR, "GFX desktop exceeds the 32766-pixel protocol limit.");
    return false;
  }
  if (!ResetSurface()) return false;
  _surface = { .width = Narrowed<std::uint32_t>(desktop.w), .height = Narrowed<std::uint32_t>(desktop.h) };
  _pixels.resize(FrameBytes(_surface));
  _headers = false;
  _progressive.reset();
  ResetAvc();
  _sources.frames.get().Resend();
  Ensures(SameSize(Whole(_surface), desktop), "surface matches the desktop");
  return true;
}
}
