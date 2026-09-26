#pragma once
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/freerdp.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace sdl_rdp::headless_client_test::client::detail::client {
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Releases;

using Clock = std::chrono::steady_clock;
using sdl_rdp::utilities::Expects;
enum class KeyState{ Down, Up };
struct Login {
  std::string_view user;
  std::string_view password;
  std::string_view domain;
};
using Pixels = std::vector<std::uint32_t>;
struct GraphicsOptions {
  bool h264                 = false;
  bool qoe_acknowledgements = false;
};
// gdi_free, called here so <freerdp/gdi/gdi.h> stays in client.cpp.
auto FreeGraphics(freerdp* instance) noexcept -> void;
using ClientInstance = std::unique_ptr<freerdp,
                                       Releases<freerdp_disconnect, FreeGraphics, freerdp_context_free, freerdp_free>>;
class Client {
public:
  explicit Client(std::uint32_t port, bool surface, std::uint32_t width = 320, std::uint32_t height = 200);
  auto     EnableGraphics(GraphicsOptions options = { })                                   -> void;
  auto     Credentials(Login const& login, bool nla = false)                               -> void;
  auto     Connect()                                                                       -> bool;
  auto     Key(std::uint16_t scancode, KeyState state)                                     -> bool;
  auto     Disconnect()                                                                    -> bool;
  auto     Pump(std::uint32_t timeout = 10)                                                -> bool;
  auto     Matches(Pixels const& pixels)                                                   -> bool;
  auto     MaxError(Pixels const& pixels) const                                            -> std::uint32_t;
  auto     Received() const                                                                -> std::uint64_t;
  auto     Until(auto ready, std::chrono::milliseconds timeout = std::chrono::seconds(10)) -> bool {
    Expects(timeout.count() > 0, "event deadline is positive");
    auto const pumped = [this] {
      return std::ranges::all_of(std::views::iota(0u, 16u), [this](std::size_t batch) { return Pump(batch ? 0 : 10); });
    };
    return sdl_rdp::utilities::Until(DeadlineAfter(timeout), pumped, ready);
  }
  auto DesktopSize() const            -> Extent;
  auto Instance() const               -> ClientInstance const&;
  auto Tolerance() const              -> std::uint32_t;
  auto Tolerance(std::uint32_t value) -> void;

private:
  std::unique_ptr<ObserverSet> observers { std::make_unique<ObserverSet>() };
  ClientInstance               instance  { freerdp_new()                   };
  std::uint32_t                tolerance = 0;
};
auto PumpInBackground(Client& client)                                        -> std::jthread;
auto Tap(Client& client, std::uint16_t scancode)                             -> void;
auto DecodedPixels(Client const& client)                                     -> std::span<std::uint32_t const>;
auto UntilDesktop(Client& client, std::uint32_t width, std::uint32_t height) -> bool;
auto UntilMatches(Client& client, Pixels const& pixels)                      -> bool;
}

namespace sdl_rdp::headless_client_test::client {
using detail::client::Client;
using detail::client::Clock;
using detail::client::DecodedPixels;
using detail::client::KeyState;
using detail::client::Pixels;
using detail::client::Tap;
using detail::client::UntilDesktop;
using detail::client::UntilMatches;
}
