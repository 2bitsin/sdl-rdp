#pragma once
#include <spawn.h>

namespace SampleGate {
class SpawnActions {
public:
       SpawnActions();
       SpawnActions(SpawnActions const&)                     = delete;
       SpawnActions(SpawnActions&&)                          = delete;
       ~SpawnActions();
  auto operator = (SpawnActions const&)     -> SpawnActions& = delete;
  auto operator = (SpawnActions&&)          -> SpawnActions& = delete;
  auto Redirect(int descriptor, int target) -> void;
  auto Get() const                          -> posix_spawn_file_actions_t const*;

private:
  posix_spawn_file_actions_t actions{ };
};
}
