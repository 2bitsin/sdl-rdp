#pragma once
#include "../sdl-rdp-backend.h"
#include "../support.test/child-process.hpp"
#include "descriptor.hpp"

#include <array>
#include <chrono>
#include <optional>

namespace BackendGate {
class WaitingOpen {
public:
  explicit                         WaitingOpen(sdlrdp_config const& config);
                                   WaitingOpen(WaitingOpen const&) = delete;
                                   WaitingOpen(WaitingOpen&&)      = delete;
                                   ~WaitingOpen();
  WaitingOpen&                     operator=(WaitingOpen const&)   = delete;
  WaitingOpen&                     operator=(WaitingOpen&&)        = delete;
  [[nodiscard]] std::optional<int> Receive(std::chrono::milliseconds timeout) const;

private:
  std::array<Backend::Descriptor, 2> sockets;
  Headless::ChildProcess             process;
};
}
