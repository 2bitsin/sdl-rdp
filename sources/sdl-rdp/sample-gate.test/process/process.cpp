#include <sdl-rdp/sample-gate.test/process/process.hpp>

#include <sdl-rdp/sample-gate.test/process/spawn-actions.hpp>

#include <sdl-rdp/utilities/descriptor.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iterator>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace sdl_rdp::sample_gate_test::process::detail::process {
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using namespace std::chrono_literals;

namespace {
auto Spawn(std::vector<std::string> arguments, int& output) -> pid_t {
  Expects(!arguments.empty(), "child arguments supplied");
  std::array<int, 2> descriptors { };
  auto const         piped       = pipe2(descriptors.data(), O_CLOEXEC);
  Expects(piped == 0, "stdout pipe created");
  output = descriptors[0];
  Descriptor const write_end{ descriptors[1] };
  SpawnActions     actions;
  actions.Redirect(write_end.Get(), STDERR_FILENO);
  actions.Redirect(write_end.Get(), STDOUT_FILENO);
  std::vector<char*> argv;
  std::ranges::transform(arguments, std::back_inserter(argv), [](auto& s) { return s.data(); });
  argv.push_back(nullptr);
  pid_t pid    = -1;
  auto  result = posix_spawn(&pid, "/usr/bin/env", actions.Get(), nullptr, argv.data(), environ);
  Expects(result == 0, "sample spawned");
  return pid;
}
}

Process::Process(std::vector<std::string> arguments) : pid(Spawn(std::move(arguments), output)) { }
Process::~Process() {
  if (pid > 0) {
    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
  }
  close(output);
}
auto Process::Line(std::string& line, Clock::time_point deadline) -> bool {
  Expects(output >= 0, "stdout pipe open");
  for (;;) {
    if (auto end = pending.find('\n'); end != std::string::npos) {
      line = pending.substr(0, end);
      pending.erase(0, end + 1);
      if (line.starts_with("INFO: ")) line.erase(0, 6);
      return true;
    }
    auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return false;
    pollfd descriptor{ .fd = output, .events = POLLIN, .revents = 0 };
    if (poll(&descriptor, 1, Narrowed<int>(left)) <= 0) return false;
    std::array<char, 4096> buffer { };
    auto                   count  = read(output, buffer.data(), buffer.size());
    if (count <= 0) return false;
    pending.append(buffer.data(), count);
    transcript.append(buffer.data(), count);
  }
}
auto Process::Exit() -> bool {
  Expects(pid > 0, "sample has not been reaped");
  auto deadline = Clock::now() + 10s;
  int  status   = 0;
  while (Clock::now() < deadline) {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      pid = -1;
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}
auto Process::Transcript() const -> std::string const& {
  return transcript;
}
}
