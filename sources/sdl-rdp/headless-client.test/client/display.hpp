#pragma once
#include "channels.hpp"
#include "client.hpp"
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/headless-client.test/utilities/published.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/client/disp.h>
#include <freerdp/event.h>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace sdl_rdp::headless_client_test::client::detail::display {
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::headless_client_test::utilities::Published;
using sdl_rdp::utilities::Pinned;

struct DisplayCapture {
  bool                  echo_resize = false;
  std::function<void()> finalizing;
  std::size_t           desktops    = 0;
  std::size_t           echoes      = 0;
};
class DisplayClient : private Pinned {
public:
  explicit    DisplayClient(Client& client);
              ~DisplayClient();
  static auto Monitor(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres = 400)
      -> DISPLAY_CONTROL_MONITOR_LAYOUT;
  auto Layout(std::uint32_t width, std::uint32_t height, std::uint32_t millimetres = 400) const -> bool;
  auto        Observed()    -> DisplayCapture&;
  auto        Ready() const -> bool;
  template <std::invocable<DisplayClient&> UseTy>
  static auto Of(Client& client, UseTy const& use) -> std::invoke_result_t<UseTy const&, DisplayClient&>;

private:
  auto Resize(rdpContext& context)             -> bool;
  auto Connected(DispClientContext& connected) -> void;
  DisplayCapture                                                        observed;
  Client&                                                               client;
  pDesktopResize                                                        desktop_resize;
  Published<DispClientContext>                                          channel;
  std::atomic_bool                                                      ready         { false };
  Membership<DisplayClient>                                             membership;
  ChannelSubscription<DISP_DVC_CHANNEL_NAME, &DisplayClient::Connected> connections;
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
