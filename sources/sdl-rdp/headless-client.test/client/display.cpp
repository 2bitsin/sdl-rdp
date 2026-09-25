#include <sdl-rdp/headless-client.test/client/display.hpp>

#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/gdi/gdi.h>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace sdl_rdp::headless_client_test::client::detail::display {
using sdl_rdp::freerdp_facade::FirstRefused;
using sdl_rdp::freerdp_facade::Refusal;
using sdl_rdp::utilities::Expects;
namespace {
// abi: pLoadChannels, BOOL is int
auto LoadDisplayChannel(freerdp* instance) -> int {
  Expects(instance != nullptr, "channel loading names its client");
  return LoadDynamicChannel(*instance, "disp");
}
auto Held(DispClientContext& channel) -> DisplayClient& {
  Expects(channel.custom != nullptr, "the display channel carries its observer");
  return *static_cast<DisplayClient*>(channel.custom);
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
  Expects(context != nullptr, "the channel event names its client context");
  Expects(event != nullptr, "channel event is supplied");
  if (std::string_view(event->name) != DISP_DVC_CHANNEL_NAME) return;
  auto* channel = static_cast<DispClientContext*>(event->pInterface);
  Expects(channel != nullptr, "the display channel interface exists");
  ObserverSet::Of(*static_cast<rdpContext*>(context)).Held<DisplayClient>()->Connected(*channel);
}
DisplayClient::DisplayClient(Client& client) : client(client), desktop_resize(ClientUpdates(client).DesktopResize) {
  auto& context = ClientContext(client);
  ObserverSet::Of(context).Add(*this);
  Callbacks::Install(ClientUpdates(client));
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 2> const display_control{ {
      { FreeRDP_SupportDisplayControl     , true },
      { FreeRDP_SynchronousDynamicChannels, true },
  } };

  auto const refused_display_control = FirstRefused(*context.settings, display_control);
  Expects(!refused_display_control.has_value(), Refusal("display control", refused_display_control));
  PubSub_SubscribeChannelConnected(context.pubSub, Callbacks::ChannelConnected);
  ClientHandle(client).LoadChannels = LoadDisplayChannel;
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
  auto& connected = channel.Get();
  auto  monitor   = Monitor(width, height, millimetres);
  return connected.SendMonitorLayout(&connected, 1, &monitor) == CHANNEL_RC_OK;
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
    Expects(context != nullptr, "capabilities name their channel");
    Held(*context).ready = true;
    return CHANNEL_RC_OK;
  };
  channel.Publish(connected);
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
