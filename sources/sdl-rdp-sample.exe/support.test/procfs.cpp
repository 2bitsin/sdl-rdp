#include "support.test/procfs.hpp"

#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp-backend.so/_detail/contract.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace SampleGate {
namespace fs = std::filesystem;

namespace {
auto ProcfsPort(std::string_view address) -> unsigned {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<unsigned>(address, ":", oxbox::utilities::Radix::HEX),
                             "a procfs socket address ends in a hex port");
}

auto ProcId() -> pid_t {
  // procfs can belong to an outer PID namespace; its children file uses that namespace.
  std::ifstream children("/proc/thread-self/children");
  pid_t         child    = 0;
  utilities::Expects(bool(children >> child), "spawned child visible in procfs");
  return child;
}
}

auto ProcfsSelf() -> pid_t {
  return utilities::Required(oxbox::utilities::ParseNumber<pid_t>(fs::read_symlink("/proc/self").string()),
                             "/proc/self links to a process id");
}

auto ListeningPort(pid_t pid) -> unsigned {
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
    if (values[3] == "0A" && std::ranges::contains(sockets, "socket:[" + values[9] + "]")) return ProcfsPort(values[1]);
  }
  return 0;
}
}
