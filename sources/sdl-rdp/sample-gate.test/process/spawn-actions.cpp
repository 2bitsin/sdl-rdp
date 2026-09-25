#include <sdl-rdp/sample-gate.test/process/spawn-actions.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <unistd.h>
#include <vector>

namespace sdl_rdp::sample_gate_test::process::detail::spawn_actions {
using sdl_rdp::utilities::Expects;

SpawnActions::SpawnActions() {
  auto const initialized = posix_spawn_file_actions_init(actions.get());
  Expects(initialized == 0, "spawn actions initialized");
}
SpawnActions::~SpawnActions() {
  posix_spawn_file_actions_destroy(actions.get());
}
auto SpawnActions::Redirect(int descriptor, int target) -> void {
  auto const redirected = posix_spawn_file_actions_adddup2(actions.get(), descriptor, target);
  Expects(redirected == 0, "child output is redirected");
}
auto SpawnActions::Spawn(std::string const& program, std::span<std::string> arguments) const -> pid_t {
  Expects(!arguments.empty(), "child arguments supplied");
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t      pid    = -1;
  auto const result = posix_spawn(&pid, program.c_str(), actions.get(), nullptr, argv.data(), environ);
  Expects(result == 0, "child spawned");
  return pid;
}
}
