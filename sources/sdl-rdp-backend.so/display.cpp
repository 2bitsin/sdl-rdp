#include "_detail/state.hpp"
#include <freerdp/channels/wtsvc.h>
#include <freerdp/settings.h>

namespace Backend {
bool Peer::OpenDisplayControl()
{
  if (disp_open || !freerdp_settings_get_bool(client->context->settings, FreeRDP_SupportDisplayControl)
      || WTSVirtualChannelManagerGetDrdynvcState(channels) != DRDYNVC_STATE_READY) return true;
  disp.reset(disp_server_context_new(channels));
  if (!disp) return false;
  disp->custom = this;
  disp->rdpcontext = client->context;
  disp->DispMonitorLayout = Layout;
  disp->MaxNumMonitors = 16;
  disp->MaxMonitorAreaFactorA = disp->MaxMonitorAreaFactorB = 8192;
  WTSVirtualChannelManagerSetDVCCreationCallback(channels,
    [](void* user, UINT32, INT32 status) -> BOOL {
      auto& peer = *static_cast<Peer*>(user);
      return status < 0 || peer.disp->DisplayControlCaps(peer.disp.get()) == CHANNEL_RC_OK;
    }, this);
  disp_open = disp->Open(disp.get()) == CHANNEL_RC_OK;
  return disp_open;
}
UINT Peer::Layout(DispServerContext* context, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const* pdu)
{
  Expects(context && pdu, "display layout exists");
  auto& self = *static_cast<Peer*>(context->custom);
  if (!pdu->NumMonitors || !self.active) return CHANNEL_RC_OK;
  int64_t left = 0, top = 0, right = 0, bottom = 0;
  for (unsigned i = 0; i < pdu->NumMonitors; ++i) {
    auto const& monitor = pdu->Monitors[i];
    left = std::min(left, int64_t(monitor.Left));
    top = std::min(top, int64_t(monitor.Top));
    right = std::max(right, int64_t(monitor.Left) + monitor.Width);
    bottom = std::max(bottom, int64_t(monitor.Top) + monitor.Height);
  }
  self.owner.Push({.type = SDLRDP_SCREEN, .screen = {unsigned(right - left), unsigned(bottom - top)}});
  return CHANNEL_RC_OK;
}
}
