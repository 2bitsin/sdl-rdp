#include <sdl-rdp/sample-gate.test/initialized-sdl.hpp>

#include <SDL3/SDL_init.h>

namespace SampleGate {
auto InitializeSdl(std::function<bool()> const& initialize) -> bool {
  return initialize();
}
auto QuitSdl([[maybe_unused]] bool initialized) noexcept -> void {
  SDL_Quit();
}
}
