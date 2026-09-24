#pragma once
#include "contract.hpp"
#include "release-client.hpp"

#include <chrono>
#include <freerdp/freerdp.h>
#include <memory>
#include <vector>

namespace Headless {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
class Client {
public:
  explicit Client(unsigned port, bool surface, unsigned width = 320, unsigned height = 200);
  void     EnableGraphics(bool h264 = false) const;
  void     Credentials(char const* user, char const* password, char const* domain, bool nla = false) const;
  bool     Pump(unsigned timeout = 10) const;
  bool     Matches(std::vector<UINT32> const& pixels);
  unsigned MaxError(std::vector<UINT32> const& pixels, std::vector<UINT32> const* reference = nullptr) const;
  UINT64   Received() const;
  bool     Until(auto ready, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    Expects(timeout.count() > 0, "event deadline is positive");
    auto deadline = Clock::now() + timeout;
    while (!ready() && Clock::now() < deadline) {
      for (unsigned batch = 0; batch < 16; ++batch)
        if (!Pump(batch ? 0 : 10)) return false;
    }
    return ready();
  }
  std::unique_ptr<freerdp, ReleaseClient> const& Instance() const;
  unsigned                                       Tolerance() const;
  void                                           Tolerance(unsigned value);

private:
  std::unique_ptr<freerdp, ReleaseClient> instance  { freerdp_new() };
  unsigned                                tolerance = 0;
};
}
