#include "_detail/display-client.hpp"

#include "_detail/client-channels.hpp"

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/gdi/gdi.h>
#include <string_view>

namespace Headless {
namespace {
BOOL LoadDisplayChannel(freerdp* instance) {
  return LoadDynamicChannel(instance, "disp");
}
}

DisplayClient::DisplayClient(Client& client)
    : client(client), desktop_resize(client.Instance()->context->update->DesktopResize) {
  Expects(!active, "one display observer per thread");
  active                                            = this;
  client.Instance()->context->update->DesktopResize = Resize;
  channel                                           = nullptr;
  ready                                             = false;
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto* context = client.Instance()->context;
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SupportDisplayControl, TRUE), "display control enabled");
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SynchronousDynamicChannels, TRUE),
          "display control enabled");
  PubSub_SubscribeChannelConnected(context->pubSub, Connected);
  client.Instance()->LoadChannels = LoadDisplayChannel;
}
DisplayClient::~DisplayClient() {
  freerdp_disconnect(client.Instance().get());
  client.Instance()->context->update->DesktopResize = desktop_resize;
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
  active  = nullptr;
  channel = nullptr;
  ready   = false;
}
DISPLAY_CONTROL_MONITOR_LAYOUT DisplayClient::Monitor(unsigned width, unsigned height, unsigned millimetres) {
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
  monitor.Flags              = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width              = width;
  monitor.Height             = height;
  monitor.PhysicalWidth      = millimetres;
  monitor.PhysicalHeight     = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  return monitor;
}
bool DisplayClient::Layout(unsigned width, unsigned height) {
  Expects(active, "observer is installed");
  Expects(ready, "channel handshake is complete");
  Expects(channel, "channel is installed");
  auto monitor = Monitor(width, height);
  return channel.load()->SendMonitorLayout(channel.load(), 1, &monitor) == CHANNEL_RC_OK;
}
DisplayCapture& DisplayClient::Observed() {
  return observed;
}
bool DisplayClient::Ready() {
  return ready.load();
}
DispClientContext* DisplayClient::Channel() {
  return channel.load();
}
BOOL DisplayClient::Resize(rdpContext* context) {
  Expects(active != nullptr, "display observer exists");
  ++active->observed.desktops;
  if (!active->desktop_resize(context)) return FALSE;
  if (active->observed.echo_resize && ready) {
    ++active->observed.echoes;
    if (!Layout(context->gdi->width, context->gdi->height)) return FALSE;
  }
  if (active->observed.finalizing) active->observed.finalizing();
  return TRUE;
}
void DisplayClient::Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) {
  if (std::string_view(event->name) != DISP_DVC_CHANNEL_NAME) return;
  channel                            = static_cast<DispClientContext*>(event->pInterface);
  channel.load()->DisplayControlCaps = [](DispClientContext*, UINT32, UINT32, UINT32) -> UINT {
    ready = true;
    return CHANNEL_RC_OK;
  };
}
}
