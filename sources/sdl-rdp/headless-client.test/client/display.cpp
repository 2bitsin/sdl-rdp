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
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::headless_client_test::client::SettingsOf;
using sdl_rdp::headless_client_test::utilities::Delegated;
using sdl_rdp::utilities::Expects;
namespace {
auto Held(DispClientContext& channel) -> DisplayClient& {
  Expects(channel.custom != nullptr, "the display channel carries its observer");
  return *static_cast<DisplayClient*>(channel.custom);
}
}
DisplayClient::DisplayClient(Client& client)
    : client(client), desktop_resize(ClientUpdates(client).DesktopResize), membership(ClientContext(client), *this),
      connections(ClientContext(client)) {
  ClientUpdates(client).DesktopResize = Delegated<&DisplayClient::Resize>;
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  std::array<std::pair<BoolKey, bool>, 2> const display_control{ {
      { BoolKey::SupportDisplayControl     , true },
      { BoolKey::SynchronousDynamicChannels, true },
  } };

  SettingsOf(client).Apply(display_control);
  ClientHandle(client).LoadChannels = ChannelLoader<LoadDynamicChannel, "disp">;
}
DisplayClient::~DisplayClient() {
  client.Disconnect();
  ClientUpdates(client).DesktopResize = desktop_resize;
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
