#pragma once
#include "input-client.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <freerdp/input.h>
#include <freerdp/update.h>
#include <fstream>
#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <poll.h>
#include <ranges>
#include <sdl-rdp-backend.so/_detail/client.hpp>
#include <sdl-rdp-backend.so/_detail/test-logs.hpp>
#include <span>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <winpr/wlog.h>

namespace SampleGate {
using Headless::Client;
using Headless::Clock;
using utilities::Expects;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

inline fs::path BuildRoot() {
  auto* path = std::getenv("PATH");
  Expects(path != nullptr, "ctest supplies PATH");
  for (auto part : std::string_view(path) | std::views::split(':')) {
    auto directory = fs::path(std::string_view(part));
    if (fs::is_regular_file(directory / "sdl-rdp-sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return { };
}

inline pid_t Spawn(std::vector<std::string> arguments, int& output) {
  Expects(!arguments.empty(), "child arguments supplied");
  std::array<int, 2> descriptors{ };
  Expects(pipe2(descriptors.data(), O_CLOEXEC) == 0, "stdout pipe created");
  output = descriptors[0];
  pid_t                      pid     = -1;
  posix_spawn_file_actions_t actions;
  Expects(posix_spawn_file_actions_init(&actions) == 0, "spawn actions initialized");
  Expects(posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO) == 0, "child stderr is redirected");
  Expects(posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO) == 0, "child stdout is redirected");
  std::vector<char*> argv;
  std::ranges::transform(arguments, std::back_inserter(argv), [](auto& s) { return s.data(); });
  argv.push_back(nullptr);
  auto result = posix_spawn(&pid, "/usr/bin/env", &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(descriptors[1]);
  Expects(result == 0, "sample spawned");
  return pid;
}

class Process {
public:
  explicit Process(std::vector<std::string> arguments) : pid(Spawn(std::move(arguments), output)) { }
           Process(Process const&) = delete;
           Process(Process&&)      = delete;
           ~Process() {
    if (pid > 0) {
      kill(pid, SIGKILL);
      while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
    close(output);
  }
  Process& operator = (Process const&) = delete;
  Process& operator = (Process&&)      = delete;
  bool     Line(std::string& line, Clock::time_point deadline) {
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
      if (poll(&descriptor, 1, int(left)) <= 0) return false;
      std::array<char, 4096> buffer { };
      auto                   count  = read(output, buffer.data(), buffer.size());
      if (count <= 0) return false;
      pending.append(buffer.data(), count);
      transcript.append(buffer.data(), count);
    }
  }
  bool Exit() {
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
  std::string const& Transcript() const { return transcript; }

private:
  std::string transcript;
  int         output     = -1;
  pid_t       pid        = -1;
  std::string pending;
};

inline std::vector<std::string> Arguments(fs::path const& certificates, bool wait) {
  Expects(fs::is_directory(certificates), "certificate directory exists");
  auto root    = BuildRoot();
  auto backend = root / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
  Expects(fs::is_regular_file(backend), "built backend exists");
  return { "env",
           "SDL_VIDEO_DRIVER=rdp",
           "SDL_RDP_PORT=0",
           "SDL_RDP_BIND=127.0.0.1",
           "SDL_RDP_CERT_DIR=" + certificates.string(),
           "SDL_RDP_BACKEND=" + backend.string(),
           "SDL_RDP_CODEC=planar",
           "SDL_RDP_WAIT_FOR_CLIENT=" + std::to_string(wait),
           (root / "bin/sdl-rdp-sample").string() };
}

inline pid_t ProcfsSelf() {
  return utilities::Required(oxbox::utilities::ParseNumber<pid_t>(fs::read_symlink("/proc/self").string()),
                             "/proc/self links to a process id");
}

inline unsigned AnnouncedPort(std::string_view line) {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<unsigned>(line, "port "),
                             "the sample announces its port as a whole number");
}

inline unsigned ProcfsPort(std::string_view address) {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<unsigned>(address, ":", oxbox::utilities::Radix::HEX),
                             "a procfs socket address ends in a hex port");
}

inline pid_t ProcId() {
  // procfs can belong to an outer PID namespace; its children file uses that namespace.
  std::ifstream children("/proc/thread-self/children");
  pid_t         child    = 0;
  Expects(bool(children >> child), "spawned child visible in procfs");
  return child;
}

inline unsigned ListeningPort(pid_t pid = 0) {
  if (!pid) pid = ProcId();
  std::vector<std::string> sockets;
  for (auto const& entry : fs::directory_iterator("/proc/" + std::to_string(pid) + "/fd")) {
    std::error_code error;
    auto            target = fs::read_symlink(entry.path(), error).string();
    if (!error && target.starts_with("socket:[")) sockets.push_back(target);
  }
  std::ifstream tcp("/proc/net/tcp");
  std::string   line;
  while (std::getline(tcp, line)) {
    std::istringstream          fields(line);
    std::array<std::string, 10> values;
    std::ranges::for_each(values, [&](auto& value) { fields >> value; });
    if (values[3] == "0A" && std::ranges::contains(sockets, "socket:[" + values[9] + "]"))
      return ProcfsPort(values[1]);
  }
  return 0;
}

}
