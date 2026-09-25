#pragma once
#include <spawn.h>

namespace sdl_rdp::sample_gate_test::process::detail::spawn_actions {
class SpawnActions {
public:
       SpawnActions();
       SpawnActions(SpawnActions const&)                     = delete;
       SpawnActions(SpawnActions&&)                          = delete;
       ~SpawnActions();
  auto operator=(SpawnActions const&)       -> SpawnActions& = delete;
  auto operator=(SpawnActions&&)            -> SpawnActions& = delete;
  auto Redirect(int descriptor, int target) -> void;
  auto Get() const                          -> posix_spawn_file_actions_t const*;

private:
  posix_spawn_file_actions_t actions{ };
};
}

namespace sdl_rdp::sample_gate_test::process {
using detail::spawn_actions::SpawnActions;
}
