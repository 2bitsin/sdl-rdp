#include <sdl-rdp/headless-client.test/display-client.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/gdi/gdi.h>
#include <string_view>

namespace Headless {
namespace {
auto LoadDisplayChannel(freerdp* instance) -> BOOL {
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
  client.Disconnect();
  client.Instance()->context->update->DesktopResize = desktop_resize;
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
  active  = nullptr;
  channel = nullptr;
  ready   = false;
}
auto DisplayClient::Monitor(unsigned width, unsigned height, unsigned millimetres) -> DISPLAY_CONTROL_MONITOR_LAYOUT {
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
  monitor.Flags              = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width              = width;
  monitor.Height             = height;
  monitor.PhysicalWidth      = millimetres;
  monitor.PhysicalHeight     = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  return monitor;
}
auto DisplayClient::Layout(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres) -> bool {
  Expects(active, "observer is installed");
  Expects(ready, "channel handshake is complete");
  Expects(channel, "channel is installed");
  auto monitor = Monitor(width, height, millimetres);
  return channel.load()->SendMonitorLayout(channel.load(), 1, &monitor) == CHANNEL_RC_OK;
}
auto DisplayClient::Observed() -> DisplayCapture& {
  return observed;
}
auto DisplayClient::Ready() -> bool {
  return ready.load();
}
auto DisplayClient::Channel() -> DispClientContext* {
  return channel.load();
}
auto DisplayClient::Resize(rdpContext* context) -> BOOL {
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
auto DisplayClient::Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) -> void {
  if (std::string_view(event->name) != DISP_DVC_CHANNEL_NAME) return;
  channel                            = static_cast<DispClientContext*>(event->pInterface);
  channel.load()->DisplayControlCaps = [](DispClientContext*, UINT32, UINT32, UINT32) -> UINT {
    ready = true;
    return CHANNEL_RC_OK;
  };
}
}
