#include <sdl-rdp/sample-gate.test/client/input.hpp>

#include <oxbox/utilities/hash.hpp>
#include <array>
#include <string>

namespace sdl_rdp::sample_gate_test::client::detail::input {
using oxbox::utilities::HashString;
using sdl_rdp::headless_client_test::client::ClientHandle;
using sdl_rdp::utilities::Expects;

namespace {
auto EnableDynamicChannel(rdpSettings& settings, std::string const& name) -> void {
  std::array const names   { name.c_str() };
  auto const       enabled = freerdp_client_add_dynamic_channel(&settings, names.size(), names.data());
  Expects(enabled, "dynamic channel enabled");
}
template <class ChannelTy> auto Interface(ChannelConnectedEventArgs const& event) -> ChannelTy& {
  Expects(event.pInterface != nullptr, "the connected channel carries its interface");
  return *static_cast<ChannelTy*>(event.pInterface);
}
}

InputClient::InputClient(Client& client) : context(ClientContext(client)) {
  ObserverSet::Of(context).Add(*this);
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  EnableDynamicChannel(*context.settings, AINPUT_CHANNEL_NAME);
  EnableDynamicChannel(*context.settings, RDPEI_CHANNEL_NAME);
  PubSub_SubscribeChannelConnected(context.pubSub, ChannelConnected);
  // abi: pLoadChannels, BOOL is int
  ClientHandle(client).LoadChannels = [](freerdp* instance) -> int {
    Expects(instance != nullptr, "the loading client exists");
    auto const& loading = *instance->context;
    return freerdp_client_load_addins(loading.channels, loading.settings);
  };
}
InputClient::~InputClient() {
  PubSub_UnsubscribeChannelConnected(context.pubSub, ChannelConnected);
  ObserverSet::Of(context).Remove<InputClient>();
}
auto InputClient::ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void {
  Expects(context != nullptr, "the channel event names its client context");
  Expects(event != nullptr, "event is supplied");
  ObserverSet::Of(*static_cast<rdpContext*>(context)).Held<InputClient>()->Connected(*event);
}
auto InputClient::Connected(ChannelConnectedEventArgs const& event) -> void {
  Expects(event.name != nullptr, "event name is supplied");
  switch (HashString(event.name)) {
  case HashString(AINPUT_DVC_CHANNEL_NAME): advanced.Publish(Interface<AInputClientContext>(event)); return;
  case HashString(RDPEI_DVC_CHANNEL_NAME):  touch.Publish(Interface<RdpeiClientContext>(event)); return;
  default:                                  return;
  }
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
