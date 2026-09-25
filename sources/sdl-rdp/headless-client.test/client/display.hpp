#pragma once
#include "client.hpp"

#include <freerdp/client/disp.h>
#include <freerdp/event.h>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace sdl_rdp::headless_client_test::client::detail::display {
using sdl_rdp::headless_client_test::utilities::ObserverSet;

struct DisplayCapture {
  bool                  echo_resize = false;
  std::function<void()> finalizing;
  std::size_t           desktops    = 0;
  std::size_t           echoes      = 0;
};
class DisplayClient {
public:
              DisplayClient(DisplayClient const&)               = delete;
              DisplayClient(DisplayClient&&)                    = delete;
  explicit    DisplayClient(Client& client);
              ~DisplayClient();
  auto        operator=(DisplayClient const&) -> DisplayClient& = delete;
  auto        operator=(DisplayClient&&)      -> DisplayClient& = delete;
  static auto Monitor(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres = 400)
      -> DISPLAY_CONTROL_MONITOR_LAYOUT;
  auto Layout(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres = 400) const -> bool;
  auto        Observed()                      -> DisplayCapture&;
  auto        Ready() const                   -> bool;
  template <std::invocable<DisplayClient&> UseTy>
  static auto Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, DisplayClient&>;

private:
  class Callbacks;
  auto Resize(rdpContext& context)             -> bool;
  auto Connected(DispClientContext& connected) -> void;
  DisplayCapture                  observed;
  Client&                         client;
  pDesktopResize                  desktop_resize;
  std::atomic<DispClientContext*> channel       { nullptr };
  std::atomic_bool                ready         { false   };
};
// The lease keeps the observer registered for the whole call, so Remove waits for it.
template <std::invocable<DisplayClient&> UseTy>
auto DisplayClient::Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, DisplayClient&> {
  return std::invoke(use, *ObserverSet::Of(*client.Instance()->context).Held<DisplayClient>());
}
}

namespace sdl_rdp::headless_client_test::client {
using detail::display::DisplayClient;
}
