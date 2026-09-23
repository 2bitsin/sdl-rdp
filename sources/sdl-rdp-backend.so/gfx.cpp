#include "_detail/state.hpp"
#include <freerdp/channels/wtsvc.h>
#include <array>
#include <numeric>

namespace Backend {
GfxChannel::GfxChannel(Peer& value) : peer(value), context(rdpgfx_server_context_new(peer.channels)) {}
GfxChannel::~GfxChannel() = default;
bool GfxChannel::Open()
{
  Expects(peer.channels && peer.client && peer.client->context, "graphics peer owns its channel manager");
  if (!context) return false;
  context->custom = this;
  context->rdpcontext = peer.client->context;
  context->ChannelIdAssigned = [](RdpgfxServerContext* context, UINT32 id) -> BOOL {
    static_cast<GfxChannel*>(context->custom)->peer.gfx_id = id;
    return TRUE;
  };
  context->CapsAdvertise = Caps;
  context->FrameAcknowledge = Ack;
  context->QoeFrameAcknowledge = Qoe;
  return context->Initialize(context.get(), TRUE) && context->Open(context.get());
}
HANDLE GfxChannel::Event() const { return rdpgfx_server_get_event_handle(context.get()); }
bool GfxChannel::Pump()
{
  auto result = rdpgfx_server_handle_messages(context.get());
  return result == ERROR_NO_DATA || Check(result, "receive");
}
bool GfxChannel::Check(UINT result, char const* operation)
{
  if (result == CHANNEL_RC_OK) return true;
  peer.owner.Log(SDLRDP_LOG_ERROR, std::format("GFX {} failed: {}.", operation, result));
  return false;
}
void Peer::AnnounceConnection(sdlrdp_codec codec)
{
  if (!connection) return;
  connection->connected.codec = codec;
  owner.Push(*connection);
  owner.Push({.type = SDLRDP_SCREEN, .screen = {screen_width, screen_height}});
  connection.reset();
  SetEvent(wake.get());
}
bool Peer::GraphicsChannel()
{
  if (gfx) return gfx->Pump();
  if (gfx_attempted || !freerdp_settings_get_bool(client->context->settings, FreeRDP_SupportGraphicsPipeline)
      || WTSVirtualChannelManagerGetDrdynvcState(channels) != DRDYNVC_STATE_READY) return true;
  gfx_attempted = true;
  gfx = std::make_unique<GfxChannel>(*this);
  if (gfx->Open()) return true;
  gfx.reset();
  owner.Log(SDLRDP_LOG_WARN, "GFX channel open failed; using legacy surface bits.");
  AnnounceConnection(encoder.codec);
  return true;
}
UINT GfxChannel::Caps(RdpgfxServerContext* context, RDPGFX_CAPS_ADVERTISE_PDU const* caps)
{
  Expects(context && caps, "graphics capabilities exist");
  auto& self = *static_cast<GfxChannel*>(context->custom);
  auto advertised = std::span(caps->capsSets, caps->capsSetCount);
  if (!self.logged) {
    std::string sets;
    for (auto const& cap : advertised)
      sets += std::format(" version=0x{:08x} flags=0x{:08x};", cap.version, cap.flags);
    self.peer.owner.Log(SDLRDP_LOG_INFO, "GFX advertised sets:" + sets);
  }
  auto selected = SelectCapability(advertised);
  if (!selected.version) return ERROR_NOT_SUPPORTED;
  RDPGFX_CAPS_CONFIRM_PDU confirm{&selected};
  if (!self.Check(context->CapsConfirm(context, &confirm), "confirm")) return ERROR_INTERNAL_ERROR;
  self.confirmed = true;
  self.peer.graphics_ready_time = Peer::Clock::now() - self.peer.activated_at;
  self.width = self.height = 0;
  self.headers = false;
  self.prepared.clear();
  if (!self.Select()) return ERROR_INTERNAL_ERROR;
  self.peer.AnnounceConnection(self.peer.encoder.codec);
  std::scoped_lock lock(self.peer.owner.frame_guard);
  self.peer.pending.clear();
  self.peer.ack_enabled = true;
  self.peer.Post({0, 0, int(self.peer.owner.width), int(self.peer.owner.height)});
  if (!self.logged) self.peer.owner.Log(SDLRDP_LOG_INFO, std::format("GFX confirmed version=0x{:08x} flags=0x{:08x}.",
    selected.version, selected.flags));
  self.logged = true;
  return CHANNEL_RC_OK;
}
UINT GfxChannel::Ack(RdpgfxServerContext* context, RDPGFX_FRAME_ACKNOWLEDGE_PDU const* ack)
{
  Expects(context && ack, "graphics acknowledgement exists");
  auto& self = *static_cast<GfxChannel*>(context->custom);
  self.peer.AcceptAcknowledgement(ack->frameId);
  std::scoped_lock lock(self.peer.owner.frame_guard);
  self.queue_depth = ack->queueDepth;
  self.peer.ack_enabled = ack->queueDepth != SUSPEND_FRAME_ACKNOWLEDGEMENT;
  if (!self.peer.ack_enabled) self.peer.pending.clear();
  self.peer.owner.frame_changed.notify_all();
  SetEvent(self.peer.wake.get());
  return CHANNEL_RC_OK;
}
UINT GfxChannel::Qoe(RdpgfxServerContext* context, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* ack)
{
  Expects(context && ack, "graphics QoE acknowledgement exists");
  auto& self = *static_cast<GfxChannel*>(context->custom);
  self.peer.graphics_qoe = *ack;
  return CHANNEL_RC_OK;
}
bool GfxChannel::Budget() const
{
  Expects(peer.ack_enabled && queue_depth != SUSPEND_FRAME_ACKNOWLEDGEMENT,
    "queue depth is outstanding bytes, or zero when unavailable; acknowledgements are not suspended");
  constexpr std::size_t MinimumByteBudget = 64 * 1024, BufferedFrames = 2;
  if (!queue_depth || peer.pending.empty()) return true;
  auto bytes = std::accumulate(peer.pending.begin(), peer.pending.end(), std::size_t{},
    [](auto sum, auto const& frame) { return sum + frame.bytes; });
  auto next = prepared.empty() ? last_bytes : frame_bytes;
  return bytes + queue_depth + next <= std::max<std::size_t>(std::max(last_bytes, next) * BufferedFrames, MinimumByteBudget);
}
bool GfxChannel::Surface()
{
  Expects(confirmed, "surface follows capability confirmation");
  if (width == unsigned(peer.desktop.w) && height == unsigned(peer.desktop.h)) return true;
  constexpr int MaximumSurfaceDimension = 32766;
  if (peer.desktop.w > MaximumSurfaceDimension || peer.desktop.h > MaximumSurfaceDimension) {
    peer.owner.Log(SDLRDP_LOG_ERROR, "GFX desktop exceeds the 32766-pixel protocol limit.");
    return false;
  }
  RDPGFX_DELETE_ENCODING_CONTEXT_PDU encoding{GraphicsSurfaceId, GraphicsContextId};
  if (headers && !Check(context->DeleteEncodingContext(context.get(), &encoding), "delete encoding context")) return false;
  RDPGFX_DELETE_SURFACE_PDU remove{GraphicsSurfaceId};
  if (width && !Check(context->DeleteSurface(context.get(), &remove), "delete surface")) return false;
  constexpr UINT32 PrimaryMonitor = 1, MonitorCount = 1;
  MONITOR_DEF monitor{0, 0, peer.desktop.w - 1, peer.desktop.h - 1, PrimaryMonitor};
  RDPGFX_RESET_GRAPHICS_PDU reset{unsigned(peer.desktop.w), unsigned(peer.desktop.h), MonitorCount, &monitor};
  RDPGFX_CREATE_SURFACE_PDU create{GraphicsSurfaceId, UINT16(peer.desktop.w), UINT16(peer.desktop.h), GFX_PIXEL_FORMAT_XRGB_8888};
  RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU map{GraphicsSurfaceId, 0, 0, 0};
  if (!Check(context->ResetGraphics(context.get(), &reset), "reset graphics")
      || !Check(context->CreateSurface(context.get(), &create), "create surface")
      || !Check(context->MapSurfaceToOutput(context.get(), &map), "map surface")) return false;
  width = peer.desktop.w; height = peer.desktop.h;
  pixels.resize(std::size_t(width) * height * 4);
  headers = false;
  progressive.reset();
  peer.sending.clear();
  peer.sending.Add({0, 0, int(peer.snapshot_width), int(peer.snapshot_height)});
  Ensures(width == unsigned(peer.desktop.w) && height == unsigned(peer.desktop.h), "surface matches desktop");
  return true;
}
}
