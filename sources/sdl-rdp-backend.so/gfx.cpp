#include "_detail/gfx.hpp"

#include "_detail/acknowledgement-window.hpp"
#include "_detail/activation.hpp"
#include "_detail/callback-owner.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/encoder.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/rect.hpp"
#include "_detail/scaler.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <freerdp/channels/wtsvc.h>
#include <numeric>
#include <utility>

namespace Backend {
namespace {
constexpr int MaximumSurfaceDimension = 32766;
GfxChannel& Held(RdpgfxServerContext* context) {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<GfxChannel>(context->custom);
}
AcknowledgementMode Mode(UINT32 queue_depth) {
  return queue_depth == SUSPEND_FRAME_ACKNOWLEDGEMENT ? AcknowledgementMode::Suspended : AcknowledgementMode::Tracking;
}
bool FitsProtocol(sdlrdp_rect desktop) {
  return desktop.w <= MaximumSurfaceDimension && desktop.h <= MaximumSurfaceDimension;
}
}
GfxChannel::GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration,
                       Activation& activation, PeerFrames& frames, FramePacing& pacing, Encoder& encoder,
                       Scaler& scaler)
    : _link { link }, _diagnostics{ diagnostics }, _configuration{ configuration }, _activation{ activation },
      _frames{ frames }, _pacing{ pacing }, _encoder{ encoder }, _scaler{ scaler },
      _context{ rdpgfx_server_context_new(link.Channels()) } { }
GfxChannel::~GfxChannel() = default;
bool GfxChannel::Open() {
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  _context->ChannelIdAssigned   = [](RdpgfxServerContext* assigned, UINT32 id) -> BOOL {
    Held(assigned)._id = id;
    return TRUE;
  };
  _context->CapsAdvertise       = Caps;
  _context->FrameAcknowledge    = Ack;
  _context->QoeFrameAcknowledge = Qoe;
  return _context->Initialize(_context.get(), TRUE) && _context->Open(_context.get());
}
HANDLE GfxChannel::Event() const {
  return rdpgfx_server_get_event_handle(_context.get());
}
bool GfxChannel::Pump() {
  auto result = rdpgfx_server_handle_messages(_context.get());
  return result == ERROR_NO_DATA || Check(result, "receive");
}
bool GfxChannel::Confirmed() const noexcept {
  return _confirmed;
}
bool GfxChannel::Assigned(UINT32 channel_id) const noexcept {
  return _id == channel_id;
}
GraphicsTiming const& GfxChannel::Timing() const noexcept {
  return _timing;
}
bool GfxChannel::Check(UINT result, char const* operation) const {
  if (result == CHANNEL_RC_OK) return true;
  _diagnostics.Log(SDLRDP_LOG_ERROR, std::format("GFX {} failed: {}.", operation, result));
  return false;
}
void GfxChannel::LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const {
  if (_logged) return;
  std::string sets;
  for (auto const& cap : advertised)
    sets += std::format(" version=0x{:08x} flags=0x{:08x};", cap.version, cap.flags);
  _diagnostics.Log(SDLRDP_LOG_INFO, "GFX advertised sets:" + sets);
}
UINT GfxChannel::ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted) {
  auto const codec      = _encoder.Codec();
  bool const announcing = _activation.Holding();
  _activation.Announce(codec, _pacing.Effective());
  if (announcing && wanted && codec != SDLRDP_CODEC_AVC420) _activation.CodecChanged(codec);
  _pacing.Acknowledgements(AcknowledgementMode::Restarted);
  _frames.Refresh();
  if (!_logged)
    _diagnostics.Log(SDLRDP_LOG_INFO,
                     std::format("GFX confirmed version=0x{:08x} flags=0x{:08x}.", selected.version, selected.flags));
  _logged = true;
  return CHANNEL_RC_OK;
}
bool GfxChannel::ResetSurface() {
  RDPGFX_DELETE_ENCODING_CONTEXT_PDU const encoding{ GraphicsSurfaceId, GraphicsContextId };
  if (_headers && !Check(_context->DeleteEncodingContext(_context.get(), &encoding), "delete encoding context"))
    return false;
  RDPGFX_DELETE_SURFACE_PDU const remove{ GraphicsSurfaceId };
  if (_surface.width && !Check(_context->DeleteSurface(_context.get(), &remove), "delete surface")) return false;
  constexpr UINT32                       PrimaryMonitor = 1;
  constexpr UINT32                       MonitorCount   = 1;
  auto const                             desktop        = _scaler.Target();
  MONITOR_DEF                            monitor        { 0, 0, desktop.w - 1, desktop.h - 1, PrimaryMonitor };
  RDPGFX_RESET_GRAPHICS_PDU const reset { unsigned(desktop.w), unsigned(desktop.h), MonitorCount, &monitor };
  RDPGFX_CREATE_SURFACE_PDU const        create         { GraphicsSurfaceId, UINT16(desktop.w), UINT16(desktop.h),
                                                          GFX_PIXEL_FORMAT_XRGB_8888 };
  RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU const map            { GraphicsSurfaceId, 0, 0, 0                         };
  return Check(_context->ResetGraphics(_context.get(), &reset), "reset graphics") &&
         Check(_context->CreateSurface(_context.get(), &create), "create surface") &&
         Check(_context->MapSurfaceToOutput(_context.get(), &map), "map surface");
}
UINT GfxChannel::Caps(RdpgfxServerContext* context, RDPGFX_CAPS_ADVERTISE_PDU const* caps) {
  Expects(caps, "graphics capabilities are supplied");
  auto& self       = Held(context);
  auto  advertised = std::span(caps->capsSets, caps->capsSetCount);
  self.LogCapabilities(advertised);
  bool const wanted   = self._configuration.Codec() == SDLRDP_CODEC_AVC420;
  auto       selected = SelectCapability(advertised, true);
  if (AllowsAvc(selected) && !Avc::Encoder::Available()) selected = SelectCapability(advertised);
  if (!selected.version) return ERROR_NOT_SUPPORTED;
  RDPGFX_CAPS_CONFIRM_PDU const confirm{ &selected };
  if (!self.Check(context->CapsConfirm(context, &confirm), "confirm")) return ERROR_INTERNAL_ERROR;
  self.ConfirmedCapability(selected);
  if (!self.Select()) return ERROR_INTERNAL_ERROR;
  return self.ActivateCapabilities(selected, wanted);
}
void GfxChannel::ResetAvc() {
  _avc.Close();
  _force_idr    = true;
  _avc_rejected = false;
}
void GfxChannel::ConfirmedCapability(RDPGFX_CAPSET const& cap) {
  Expects(cap.version, "supported capabilities confirmed");
  _avc_allowed = cap.version == RDPGFX_CAPVERSION_81
                     ? (cap.flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) != 0
                     : cap.version >= RDPGFX_CAPVERSION_10 && !(cap.flags & RDPGFX_CAPS_FLAG_AVC_DISABLED) &&
                           Avc::Encoder::Available();
  ResetAvc();
  _confirmed = true;
  _timing.Ready(Activation::Clock::now() - _activation.ActivatedAt());
  _surface = { };
  _headers = false;
  _prepared.clear();
}
UINT GfxChannel::Ack(RdpgfxServerContext* context, RDPGFX_FRAME_ACKNOWLEDGE_PDU const* ack) {
  Expects(ack, "acknowledgement is supplied");
  auto& self = Held(context);
  self._pacing.Accept(ack->frameId);
  self._queue_depth = ack->queueDepth;
  self._pacing.Acknowledgements(Mode(ack->queueDepth));
  return CHANNEL_RC_OK;
}
UINT GfxChannel::Qoe(RdpgfxServerContext* context, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* ack) {
  Expects(ack, "acknowledgement is supplied");
  Held(context)._timing.Record(*ack);
  return CHANNEL_RC_OK;
}
unsigned GfxChannel::FrameWindow() const {
  Expects(_queue_depth != SUSPEND_FRAME_ACKNOWLEDGEMENT, "acknowledgements are enabled");
  // MS-RDPEGFX 2.2.2.13 reports bytes, not frames; reserve at most one slot for that backlog.
  return _queue_depth && _queue_depth >= _last_bytes ? AcknowledgedFrameWindow - 1 : AcknowledgedFrameWindow;
}
bool GfxChannel::Surface() {
  Expects(_confirmed, "surface follows capability confirmation");
  auto const desktop = _scaler.Target();
  if (SameSize(Whole(_surface), desktop)) return true;
  if (!FitsProtocol(desktop)) {
    _diagnostics.Log(SDLRDP_LOG_ERROR, "GFX desktop exceeds the 32766-pixel protocol limit.");
    return false;
  }
  if (!ResetSurface()) return false;
  _surface = { .width = unsigned(desktop.w), .height = unsigned(desktop.h) };
  _pixels.resize(FrameBytes(_surface));
  _headers = false;
  _progressive.reset();
  ResetAvc();
  _frames.Resend();
  Ensures(SameSize(Whole(_surface), desktop), "surface matches the desktop");
  return true;
}
}
