#pragma once
#include <freerdp/client/ainput.h>
#include <freerdp/client/rdpei.h>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/headless-client.test/utilities/published.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <concepts>
#include <functional>
#include <type_traits>

namespace sdl_rdp::sample_gate_test::client::detail::input {
using sdl_rdp::headless_client_test::client::ChannelSubscription;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::headless_client_test::utilities::Published;
using sdl_rdp::utilities::Pinned;

class InputClient : private Pinned {
public:
  explicit    InputClient(Client& client);
  auto        Advanced() const                -> Published<AInputClientContext> const&;
  auto        Touch() const                   -> Published<RdpeiClientContext> const&;
  auto        TouchV10() const                -> bool;
  static auto AdvancedChannel(Client& client) -> AInputClientContext&;
  template <std::invocable<InputClient const&> UseTy>
  static auto Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, InputClient const&>;

private:
  auto AdvancedConnected(AInputClientContext& channel) -> void;
  auto TouchConnected(RdpeiClientContext& channel)     -> void;
  Published<AInputClientContext>                                                advanced;
  Published<RdpeiClientContext>                                                 touch;
  Membership<InputClient>                                                       membership;
  ChannelSubscription<AINPUT_DVC_CHANNEL_NAME, &InputClient::AdvancedConnected> advanced_connections;
  ChannelSubscription<RDPEI_DVC_CHANNEL_NAME, &InputClient::TouchConnected>     touch_connections;
};
// The lease keeps the observer registered for the whole call, so Remove waits for it.
template <std::invocable<InputClient const&> UseTy>
auto InputClient::Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, InputClient const&> {
  return std::invoke(use, *ObserverSet::Of(ClientContext(client)).Held<InputClient>());
}
}

namespace sdl_rdp::sample_gate_test::client {
using detail::input::InputClient;
}
