#pragma once
#include <memory>
#include <span>
#include <spawn.h>
#include <string>

namespace sdl_rdp::sample_gate_test::process::detail::spawn_actions {
class SpawnActions {
public:
       SpawnActions();
       SpawnActions(SpawnActions const&)                                                          = delete;
       SpawnActions(SpawnActions&&)                                                               = delete;
       ~SpawnActions();
  auto operator=(SpawnActions const&)                                            -> SpawnActions& = delete;
  auto operator=(SpawnActions&&)                                                 -> SpawnActions& = delete;
  auto Redirect(int descriptor, int target)                                      -> void;
  auto Spawn(std::string const& program, std::span<std::string> arguments) const -> pid_t;

private:
  std::unique_ptr<posix_spawn_file_actions_t> actions{ std::make_unique<posix_spawn_file_actions_t>() };
};
}

namespace sdl_rdp::sample_gate_test::process {
using detail::spawn_actions::SpawnActions;
}
