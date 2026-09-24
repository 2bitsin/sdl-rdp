#include <sdl-rdp/headless-client.test/display-client.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/gdi/gdi.h>
#include <cstdint>
#include <string_view>

namespace Headless {
namespace {
// abi: pLoadChannels, BOOL is int
auto LoadDisplayChannel(freerdp* instance) -> int {
  return LoadDynamicChannel(instance, "disp");
}
auto Held(DispClientContext* channel) -> DisplayClient& {
  Expects(channel != nullptr, "callback channel exists");
  Expects(channel->custom != nullptr, "the display channel carries its observer");
  return *static_cast<DisplayClient*>(channel->custom);
}
}
class DisplayClient::Callbacks {
public:
  static auto Install(rdpUpdate& update) -> void;
  // abi: pChannelConnectedEventHandler
  static auto ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void;
};
auto DisplayClient::Callbacks::Install(rdpUpdate& update) -> void {
  // abi: pDesktopResize, BOOL is int
  update.DesktopResize = [](rdpContext* context) -> int {
    Expects(context != nullptr, "resize names its client context");
    return ObserverSet::Of(*context).Held<DisplayClient>()->Resize(*context);
  };
}
auto DisplayClient::Callbacks::ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void {
  Expects(event != nullptr, "channel event is supplied");
  if (std::string_view(event->name) != DISP_DVC_CHANNEL_NAME) return;
  auto* channel = static_cast<DispClientContext*>(event->pInterface);
  Expects(channel != nullptr, "the display channel interface exists");
  ObserverSet::Of(context).Held<DisplayClient>()->Connected(*channel);
}
DisplayClient::DisplayClient(Client& client)
    : client(client), desktop_resize(client.Instance()->context->update->DesktopResize) {
  ObserverSet::Of(*client.Instance()->context).Add(*this);
  Callbacks::Install(*client.Instance()->context->update);
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto* context = client.Instance()->context;
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SupportDisplayControl, true), "display control enabled");
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SynchronousDynamicChannels, true),
          "display control enabled");
  PubSub_SubscribeChannelConnected(context->pubSub, Callbacks::ChannelConnected);
  client.Instance()->LoadChannels = LoadDisplayChannel;
}
DisplayClient::~DisplayClient() {
  client.Disconnect();
  client.Instance()->context->update->DesktopResize = desktop_resize;
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Callbacks::ChannelConnected);
  ObserverSet::Of(*client.Instance()->context).Remove<DisplayClient>();
}
auto DisplayClient::Monitor(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres)
    -> DISPLAY_CONTROL_MONITOR_LAYOUT {
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
  monitor.Flags              = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width              = width;
  monitor.Height             = height;
  monitor.PhysicalWidth      = millimetres;
  monitor.PhysicalHeight     = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  return monitor;
}
auto DisplayClient::Layout(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres) const -> bool {
  Expects(ready, "channel handshake is complete");
  auto* connected = channel.load();
  Expects(connected != nullptr, "channel is installed");
  auto monitor = Monitor(width, height, millimetres);
  return connected->SendMonitorLayout(connected, 1, &monitor) == CHANNEL_RC_OK;
}
auto DisplayClient::Observed() -> DisplayCapture& {
  return observed;
}
auto DisplayClient::Ready() const -> bool {
  return ready.load();
}
auto DisplayClient::Connected(DispClientContext& connected) -> void {
  connected.custom = this;
  // abi: pcDispCaps, UINT32 and UINT are uint32_t
  connected.DisplayControlCaps = [](DispClientContext* context, std::uint32_t, std::uint32_t,
                                    std::uint32_t) -> std::uint32_t {
    Held(context).ready = true;
    return CHANNEL_RC_OK;
  };
  channel                      = &connected;
}
auto DisplayClient::Resize(rdpContext& context) -> bool {
  ++observed.desktops;
  if (!desktop_resize(&context)) return false;
  if (observed.echo_resize && ready) {
    ++observed.echoes;
    if (!Layout(context.gdi->width, context.gdi->height)) return false;
  }
  if (observed.finalizing) observed.finalizing();
  return true;
}
}
