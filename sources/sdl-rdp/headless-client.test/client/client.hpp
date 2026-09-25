#pragma once
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/freerdp.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>

namespace Headless {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
enum class KeyState{ Down, Up };
struct GraphicsOptions {
  bool h264                 = false;
  bool qoe_acknowledgements = false;
};
// gdi_free, called here so <freerdp/gdi/gdi.h> stays in client.cpp.
auto FreeGraphics(freerdp* instance) noexcept -> void;
using ClientInstance = std::unique_ptr<
    freerdp, Backend::Releases<freerdp_disconnect, FreeGraphics, freerdp_context_free, freerdp_free>>;
class Client {
public:
  explicit Client(std::uint32_t port, bool surface, std::uint32_t width = 320, std::uint32_t height = 200);
  auto EnableGraphics(GraphicsOptions options = { }) const                                             -> void;
  auto Credentials(char const* user, char const* password, char const* domain, bool nla = false) const -> void;
  auto Connect() const                                                                                 -> bool;
  auto Key(std::uint16_t scancode, KeyState state) const                                               -> bool;
  auto Disconnect() const                                                                              -> bool;
  auto Pump(std::uint32_t timeout = 10) const                                                          -> bool;
  auto Matches(std::vector<std::uint32_t> const& pixels)                                               -> bool;
  auto MaxError(std::vector<std::uint32_t> const& pixels, std::vector<std::uint32_t> const* reference = nullptr) const
      -> std::uint32_t;
  auto Received() const                                                                                -> std::uint64_t;
  auto Until(auto ready, std::chrono::milliseconds timeout = std::chrono::seconds(10))                 -> bool {
    Expects(timeout.count() > 0, "event deadline is positive");
    auto const pumped = [this] {
      return std::ranges::all_of(std::views::iota(0u, 16u), [this](std::size_t batch) { return Pump(batch ? 0 : 10); });
    };
    return Backend::Until(Backend::DeadlineAfter(timeout), pumped, ready);
  }
  auto UntilDesktop(std::uint32_t width, std::uint32_t height) -> bool;
  auto Instance() const                                        -> ClientInstance const&;
  auto Tolerance() const                                       -> std::uint32_t;
  auto Tolerance(std::uint32_t value)                          -> void;

private:
  std::unique_ptr<ObserverSet> observers { std::make_unique<ObserverSet>() };
  ClientInstance               instance  { freerdp_new()                   };
  std::uint32_t                tolerance = 0;
};
auto PumpInBackground(Client const& client)            -> std::jthread;
auto Tap(Client const& client, std::uint16_t scancode) -> void;
}
