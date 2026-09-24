#include <sdl-rdp/sample-gate.test/input-client.hpp>

#include <array>
#include <string_view>

namespace SampleGate {
using utilities::Expects;

InputClient::InputClient(Headless::Client& client) {
  advanced = nullptr;
  touch    = nullptr;
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto*                      context = client.Instance()->context;
  std::array<char const*, 1> ainput  { AINPUT_CHANNEL_NAME };
  std::array<char const*, 1> rdpei   { RDPEI_CHANNEL_NAME  };
  Expects(freerdp_client_add_dynamic_channel(context->settings, 1, ainput.data()), "ainput enabled");
  Expects(freerdp_client_add_dynamic_channel(context->settings, 1, rdpei.data()), "rdpei enabled");
  // abi: pChannelConnectedEventHandler
  PubSub_SubscribeChannelConnected(context->pubSub, [](void* /*unused*/, ChannelConnectedEventArgs const* event) {
    Expects(event, "event is supplied");
    Expects(event->name, "event name is supplied");
    auto name = std::string_view(event->name);
    if (name == AINPUT_DVC_CHANNEL_NAME) advanced = static_cast<AInputClientContext*>(event->pInterface);
    if (name == RDPEI_DVC_CHANNEL_NAME) touch = static_cast<RdpeiClientContext*>(event->pInterface);
  });
  // abi: pLoadChannels, BOOL is int
  client.Instance()->LoadChannels = [](freerdp* instance) -> int {
    return freerdp_client_load_addins(instance->context->channels, instance->context->settings);
  };
}
auto InputClient::Advanced() -> std::atomic<AInputClientContext*> const& {
  return advanced;
}
auto InputClient::Touch() -> std::atomic<RdpeiClientContext*> const& {
  return touch;
}
}
