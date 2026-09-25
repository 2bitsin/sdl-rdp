#include <sdl-rdp/sample-gate.test/process/spawn-actions.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::sample_gate_test::process::detail::spawn_actions {
using sdl_rdp::utilities::Expects;

SpawnActions::SpawnActions() {
  auto const initialized = posix_spawn_file_actions_init(&actions);
  Expects(initialized == 0, "spawn actions initialized");
}
SpawnActions::~SpawnActions() {
  posix_spawn_file_actions_destroy(&actions);
}
auto SpawnActions::Redirect(int descriptor, int target) -> void {
  auto const redirected = posix_spawn_file_actions_adddup2(&actions, descriptor, target);
  Expects(redirected == 0, "child output is redirected");
}
auto SpawnActions::Get() const -> posix_spawn_file_actions_t const* {
  return &actions;
}
}
