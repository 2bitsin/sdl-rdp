#pragma once
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/utilities/child-process.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <array>
#include <chrono>
#include <optional>

namespace sdl_rdp::headless_client_test::backend::detail::waiting_open {
using sdl_rdp::utilities::Pinned;
using sdl_rdp::configuration::Setup;
using sdl_rdp::headless_client_test::utilities::ChildProcess;
using sdl_rdp::utilities::Descriptor;

class WaitingOpen : private Pinned {
public:
  explicit           WaitingOpen(Setup const& config);
                     ~WaitingOpen();
  [[nodiscard]] auto Receive(std::chrono::milliseconds timeout) const -> std::optional<int>;

private:
  std::array<Descriptor, 2> sockets;
  ChildProcess              process;
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::waiting_open::WaitingOpen;
}
