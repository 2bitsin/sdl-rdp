#pragma once
#include "child-process.hpp"
#include <sdl-rdp/utilities/descriptor.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <array>
#include <chrono>
#include <optional>

namespace BackendGate {
class WaitingOpen {
public:
  explicit           WaitingOpen(sdlrdp_config const& config);
                     WaitingOpen(WaitingOpen const&)                                  = delete;
                     WaitingOpen(WaitingOpen&&)                                       = delete;
                     ~WaitingOpen();
  auto               operator=(WaitingOpen const&)                    -> WaitingOpen& = delete;
  auto               operator=(WaitingOpen&&)                         -> WaitingOpen& = delete;
  [[nodiscard]] auto Receive(std::chrono::milliseconds timeout) const -> std::optional<int>;

private:
  std::array<Backend::Descriptor, 2> sockets;
  Headless::ChildProcess             process;
};
}
