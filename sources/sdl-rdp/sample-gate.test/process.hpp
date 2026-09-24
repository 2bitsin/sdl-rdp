#pragma once
#include <sdl-rdp/headless-client.test/client.hpp>
#include <string>
#include <sys/types.h>
#include <vector>

namespace SampleGate {
class Process {
public:
  explicit Process(std::vector<std::string> arguments);
           Process(Process const&)                                                   = delete;
           Process(Process&&)                                                        = delete;
           ~Process();
  auto     operator=(Process const&)                                     -> Process& = delete;
  auto     operator=(Process&&)                                          -> Process& = delete;
  auto     Line(std::string& line, Headless::Clock::time_point deadline) -> bool;
  auto     Exit()                                                        -> bool;
  auto     Transcript() const                                            -> std::string const&;

private:
  std::string transcript;
  int         output     = -1;
  pid_t       pid        = -1;
  std::string pending;
};
}
