#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>

#include <SDL3/SDL_init.h>

namespace sdl_rdp::sample_gate_test::process::detail::initialized_sdl {
auto InitializeSdl(std::function<bool()> const& initialize) -> bool {
  return initialize();
}
auto QuitSdl([[maybe_unused]] bool initialized) noexcept -> void {
  SDL_Quit();
}
}
