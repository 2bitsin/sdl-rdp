#include <sdl-rdp/headless-client.test/utilities/child-process.hpp>

#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.posix.hpp>

#include <csignal>
#include <string_view>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace sdl_rdp::headless_client_test::utilities::detail::child_process {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::SystemCall;

namespace {
constexpr int BodyThrew = 124;

auto Run(std::function<int()> const& body) noexcept -> int {
  return Contained(BodyThrew, body, [](std::string_view) noexcept { });
}
auto Spawned(std::function<int()> const& body) -> pid_t {
  Expects(static_cast<bool>(body), "the child has a body");
  auto const pid = SystemCall(::fork(), "fork");
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
  Expects(pid != Reaped, "the child is not reaped yet");
  int        status = 0;
  auto const reaped = waitpid(std::exchange(pid, Reaped), &status, 0);
  Ensures(reaped > 0, "the child is reaped");
  return status;
}
auto ChildProcess::ExitedCleanly() -> bool {
  auto const status = Wait();
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
auto ChildProcess::Kill() const noexcept -> void {
  if (pid != Reaped) ::kill(pid, SIGKILL);
}
}
