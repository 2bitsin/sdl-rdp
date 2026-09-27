#include <sdl-rdp/sample-gate.test/client/input.hpp>

#include <freerdp/client/channels.h>
#include <array>
#include <string>

namespace sdl_rdp::sample_gate_test::client::detail::input {
using sdl_rdp::headless_client_test::client::ClientHandle;
using sdl_rdp::utilities::Expects;

namespace {
auto EnableDynamicChannel(rdpSettings& settings, std::string const& name) -> void {
  std::array const names   { name.c_str() };
  auto const       enabled = freerdp_client_add_dynamic_channel(&settings, names.size(), names.data());
  Expects(enabled, "dynamic channel enabled");
}
}

InputClient::InputClient(Client& client)
    : membership(ClientContext(client), *this), advanced_connections(ClientContext(client)),
      touch_connections(ClientContext(client)) {
  auto& settings = *ClientContext(client).settings;
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  EnableDynamicChannel(settings, AINPUT_CHANNEL_NAME);
  EnableDynamicChannel(settings, RDPEI_CHANNEL_NAME);
  // abi: pLoadChannels, BOOL is int
  ClientHandle(client).LoadChannels = [](freerdp* instance) -> int {
    Expects(instance != nullptr, "the loading client exists");
    auto const& loading = *instance->context;
    return freerdp_client_load_addins(loading.channels, loading.settings);
  };
}
auto InputClient::AdvancedConnected(AInputClientContext& channel) -> void {
  advanced.Publish(channel);
}
auto InputClient::TouchConnected(RdpeiClientContext& channel) -> void {
  touch.Publish(channel);
}
auto InputClient::Advanced() const -> Published<AInputClientContext> const& {
  return advanced;
}
auto InputClient::AdvancedChannel(Client& client) -> AInputClientContext& {
  return Of(client, [](InputClient const& input) -> AInputClientContext& { return input.Advanced().Get(); });
}
auto InputClient::Touch() const -> Published<RdpeiClientContext> const& {
  return touch;
}
auto InputClient::TouchV10() const -> bool {
  auto const channel = touch.Peek();
  return channel && channel->get().GetVersion(&channel->get()) == RDPINPUT_PROTOCOL_V10;
}
}
