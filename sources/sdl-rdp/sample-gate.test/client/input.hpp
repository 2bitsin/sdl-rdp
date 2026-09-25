#pragma once
#include <freerdp/client/ainput.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpei.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/headless-client.test/utilities/published.hpp>

#include <concepts>
#include <functional>
#include <type_traits>

namespace sdl_rdp::sample_gate_test::client::detail::input {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::headless_client_test::utilities::Published;

class InputClient {
public:
              InputClient(InputClient const&)                 = delete;
              InputClient(InputClient&&)                      = delete;
  explicit    InputClient(Client& client);
              ~InputClient();
  auto        operator=(InputClient const&)   -> InputClient& = delete;
  auto        operator=(InputClient&&)        -> InputClient& = delete;
  auto        Advanced() const                -> Published<AInputClientContext> const&;
  auto        Touch() const                   -> Published<RdpeiClientContext> const&;
  auto        TouchV10() const                -> bool;
  static auto AdvancedChannel(Client& client) -> AInputClientContext&;
  template <std::invocable<InputClient const&> UseTy>
  static auto Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, InputClient const&>;

private:
  // abi: pChannelConnectedEventHandler
  static auto ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void;
  auto        Connected(ChannelConnectedEventArgs const& event)                       -> void;
  rdpContext&                    context;
  Published<AInputClientContext> advanced;
  Published<RdpeiClientContext>  touch;
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
