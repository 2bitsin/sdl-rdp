#pragma once
#include "contract.hpp"
#include "release-client.hpp"

#include <algorithm>
#include <chrono>
#include <freerdp/freerdp.h>
#include <memory>
#include <ranges>
#include <vector>

namespace Headless {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
class Client {
public:
  explicit Client(unsigned port, bool surface, unsigned width = 320, unsigned height = 200);
  auto     EnableGraphics(bool h264 = false) const                                                         -> void;
  auto     Credentials(char const* user, char const* password, char const* domain, bool nla = false) const -> void;
  auto     Pump(unsigned timeout = 10) const                                                               -> bool;
  auto     Matches(std::vector<UINT32> const& pixels)                                                      -> bool;
  auto MaxError(std::vector<UINT32> const& pixels, std::vector<UINT32> const* reference = nullptr) const -> unsigned;
  auto     Received() const                                                                                -> UINT64;
  auto     Until(auto ready, std::chrono::milliseconds timeout = std::chrono::seconds(10))                 -> bool {
    Expects(timeout.count() > 0, "event deadline is positive");
    auto deadline = Clock::now() + timeout;
    while (!ready() && Clock::now() < deadline)
      if (!std::ranges::all_of(std::views::iota(0u, 16u), [this](unsigned batch) { return Pump(batch ? 0 : 10); }))
        return false;
    return ready();
  }
  auto Instance() const          -> std::unique_ptr<freerdp, ReleaseClient> const&;
  auto Tolerance() const         -> unsigned;
  auto Tolerance(unsigned value) -> void;

private:
  std::unique_ptr<freerdp, ReleaseClient> instance  { freerdp_new() };
  unsigned                                tolerance = 0;
};
}
