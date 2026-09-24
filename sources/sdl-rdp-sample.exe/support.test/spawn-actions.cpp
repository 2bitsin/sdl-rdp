#include "support.test/spawn-actions.hpp"

#include <sdl-rdp-backend.so/_detail/contract.hpp>

namespace SampleGate {
using utilities::Expects;

SpawnActions::SpawnActions() {
  Expects(posix_spawn_file_actions_init(&actions) == 0, "spawn actions initialized");
}
SpawnActions::~SpawnActions() {
  posix_spawn_file_actions_destroy(&actions);
}
auto SpawnActions::Redirect(int descriptor, int target) -> void {
  Expects(posix_spawn_file_actions_adddup2(&actions, descriptor, target) == 0, "child output is redirected");
}
auto SpawnActions::Get() const -> posix_spawn_file_actions_t const* {
  return &actions;
}
}
