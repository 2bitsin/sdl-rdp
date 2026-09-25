#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <string>
#include <sys/types.h>
#include <vector>

namespace sdl_rdp::sample_gate_test::process::detail::process {
using sdl_rdp::headless_client_test::client::Clock;

class Process {
public:
  explicit Process(std::vector<std::string> arguments);
           Process(Process const&)                                         = delete;
           Process(Process&&)                                              = delete;
           ~Process();
  auto     operator=(Process const&)                           -> Process& = delete;
  auto     operator=(Process&&)                                -> Process& = delete;
  auto     Line(std::string& line, Clock::time_point deadline) -> bool;
  auto     Exit()                                              -> bool;
  auto     Transcript() const                                  -> std::string const&;

private:
  std::string transcript;
  int         output     = -1;
  pid_t       pid        = -1;
  std::string pending;
};
}

namespace sdl_rdp::sample_gate_test::process {
using detail::process::Process;
}
