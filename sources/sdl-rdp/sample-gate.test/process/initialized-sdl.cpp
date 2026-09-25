#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>

#include <SDL3/SDL_init.h>

namespace sdl_rdp::sample_gate_test::process::detail::initialized_sdl {
auto InitializeSdl(std::function<bool()> const& initialize) -> bool {
  return initialize();
}
auto QuitSdl([[maybe_unused]] bool initialized) noexcept -> void {
  SDL_Quit();
}
auto LockStream(SDL_AudioStream& stream) -> StreamLock {
  return { .stream = stream, .locked = SDL_LockAudioStream(&stream) };
}
auto UnlockStream(StreamLock const& lock) noexcept -> void {
  if (lock.locked) SDL_UnlockAudioStream(&lock.stream.get());
}
}
