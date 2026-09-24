#include "support.test/contract-run.hpp"

#include "_detail/contract.hpp"
#include "_detail/test-io.hpp"
#include "support.test/child-process.hpp"

#include <gtest/gtest.h>
#include <array>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace Headless {
namespace {
constexpr int RedirectFailed = 125;
auto Redirected(std::function<int()> const& body, int errors) -> int {
  if (dup2(errors, STDERR_FILENO) < 0) return RedirectFailed;
  return body();
}
auto Spawned(std::function<int()> const& body, Descriptor const& errors) -> ChildProcess {
  return ChildProcess{ [&] { return Redirected(body, errors.Get()); } };
}
}
ContractRun::ContractRun(std::function<int()> const& body) {
  std::array<int, 2> ends  { };
  auto const         piped = pipe2(ends.data(), O_CLOEXEC);
  utilities::Expects(piped == 0, "the stderr pipe opens");
  Descriptor const input { ends[0] };
  auto             child = Spawned(body, Descriptor{ ends[1] });
  _errors = ReadText(input.Get());
  _status = child.Wait();
}
auto ContractRun::ExpectBroken(std::string_view text, int continuation) const -> void {
  using enum oxbox::platform::ContractMode;
  constexpr auto mode = utilities::detail::contract::Mode();
  switch (mode) {
  case STOP:     ExpectStopped(text); return;
  case COMPLAIN: ExpectComplained(text, continuation); return;
  case IGNORE:   ExpectIgnored(continuation); return;
  default:       utilities::Unreachable(static_cast<int>(mode));
  }
}
auto ContractRun::ExpectStopped(std::string_view text) const -> void {
  EXPECT_TRUE(Aborted()) << _status << _errors;
  EXPECT_TRUE(_errors.contains(text)) << _errors;
}
auto ContractRun::ExpectComplained(std::string_view text, int continuation) const -> void {
  EXPECT_TRUE(Exited(continuation)) << _status << _errors;
  EXPECT_TRUE(_errors.contains(text)) << _errors;
}
auto ContractRun::ExpectIgnored(int continuation) const -> void {
  EXPECT_TRUE(Exited(continuation)) << _status << _errors;
  EXPECT_TRUE(_errors.empty()) << _errors;
}
auto ContractRun::Aborted() const -> bool {
  return WIFSIGNALED(_status) && WTERMSIG(_status) == SIGABRT;
}
auto ContractRun::Exited(int code) const -> bool {
  return WIFEXITED(_status) && WEXITSTATUS(_status) == code;
}
}
