#include "_detail/input.hpp"
#include "_detail/state.hpp"

#include <freerdp/channels/wtsvc.h>
#include <freerdp/settings.h>

namespace Backend {
BOOL Peer::ActivateChannel(UINT32 id) {
  auto& peer  = *this;
  auto& input = Input::Held(peer);
  if (id == input.advanced_id) {
    input.advanced_ready = true;
    return input.advanced->Poll(input.advanced.get()) == CHANNEL_RC_OK;
  }
  if (id == input.touch_id) {
    input.touch_ready = true;
    return rdpei_server_send_sc_ready(input.touch.get(), RDPINPUT_PROTOCOL_V10, 0) == CHANNEL_RC_OK;
  }
  if (id == peer.display_id) return peer.disp->DisplayControlCaps(peer.disp.get()) == CHANNEL_RC_OK;
  return TRUE;
}
BOOL Peer::ChannelCreated(void* user, UINT32 id, INT32 status) {
  Expects(user != nullptr, "channel creation has a peer");
  auto& peer  = *static_cast<Peer*>(user);
  auto& input = Input::Held(peer);
  peer.handle_count = 0;
  if (status < 0) {
    if (id == peer.gfx_id) {
      peer.gfx.reset();
      peer.owner.Log(SDLRDP_LOG_WARN, "GFX channel rejected; using legacy surface bits.");
      peer.AnnounceConnection(peer.encoder.codec);
    }
    return TRUE;
  }
  return peer.ActivateChannel(id);
}

bool Peer::OpenDisplayControl() {
  if (disp_open || !freerdp_settings_get_bool(client->context->settings, FreeRDP_SupportDisplayControl) ||
      WTSVirtualChannelManagerGetDrdynvcState(channels) != DRDYNVC_STATE_READY)
    return true;
  disp.reset(disp_server_context_new(channels));
  if (!disp) return false;
  disp->custom            = this;
  disp->rdpcontext        = client->context;
  disp->DispMonitorLayout = Layout;
  disp->ChannelIdAssigned = [](DispServerContext* context, UINT32 id) -> BOOL {
    static_cast<Peer*>(context->custom)->display_id = id;
    return TRUE;
  };
  disp->MaxNumMonitors        = 16;
  disp->MaxMonitorAreaFactorA = disp->MaxMonitorAreaFactorB = 8192;
  disp_open                                                 = disp->Open(disp.get()) == CHANNEL_RC_OK;
  return disp_open;
}
UINT Peer::Layout(DispServerContext* context, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const* pdu) {
  Expects(context, "callback context exists");
  Expects(pdu, "display layout PDU is supplied");
  auto& self = *static_cast<Peer*>(context->custom);
  if (!pdu->NumMonitors || !self.active) return CHANNEL_RC_OK;
  int64_t left   = 0;
  int64_t top    = 0;
  int64_t right  = 0;
  int64_t bottom = 0;
  for (unsigned i = 0; i < pdu->NumMonitors; ++i) {
    auto const& monitor = pdu->Monitors[i];
    left                = std::min(left, int64_t(monitor.Left));
    top                 = std::min(top, int64_t(monitor.Top));
    right               = std::max(right, int64_t(monitor.Left) + monitor.Width);
    bottom              = std::max(bottom, int64_t(monitor.Top) + monitor.Height);
  }
  if (right - left == self.desktop.w && bottom - top == self.desktop.h) return CHANNEL_RC_OK;
  self.owner.Push(
      { .type = SDLRDP_SCREEN, .screen = { .width = unsigned(right - left), .height = unsigned(bottom - top) } });
  return CHANNEL_RC_OK;
}
}
