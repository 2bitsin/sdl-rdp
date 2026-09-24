#include "support.test/child-process.hpp"

#include "_detail/contract.hpp"
#include "_detail/system-call.hpp"

#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace Headless {
namespace {
constexpr int BodyThrew = 124;

auto Run(std::function<int()> const& body) noexcept -> int {
  try {
    return body();
  } catch (...) {
    return BodyThrew;
  }
}
auto Spawned(std::function<int()> const& body) -> pid_t {
  utilities::Expects(static_cast<bool>(body), "the child has a body");
  auto const pid = Backend::SystemCall(::fork(), "fork");
  if (pid == 0) ::_exit(Run(body));
  return pid;
}
}
ChildProcess::ChildProcess(std::function<int()> const& body) : pid(Spawned(body)) { }
ChildProcess::ChildProcess(ChildProcess&& other) noexcept : pid(std::exchange(other.pid, Reaped)) { }
ChildProcess::~ChildProcess() {
  if (pid != Reaped) std::ignore = Wait();
}
auto ChildProcess::Wait() -> int {
  utilities::Expects(pid != Reaped, "the child is not reaped yet");
  int        status = 0;
  auto const reaped = waitpid(std::exchange(pid, Reaped), &status, 0);
  utilities::Ensures(reaped > 0, "the child is reaped");
  return status;
}
auto ChildProcess::ExitedCleanly() -> bool {
  auto const status = Wait();
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
}
