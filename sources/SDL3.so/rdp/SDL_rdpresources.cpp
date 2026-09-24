#include "SDL_rdpresources.hpp"
#include <stdexcept>
namespace rdp {
auto LockMutex(SDL_Mutex& mutex) -> SDL_Mutex& {
  SDL_LockMutex(&mutex);
  return mutex;
}
void UnlockMutex(SDL_Mutex& mutex) noexcept { SDL_UnlockMutex(&mutex); }
auto LockProperties(SDL_PropertiesID properties) -> SDL_PropertiesID {
  if (!SDL_LockProperties(properties)) throw std::runtime_error(SDL_GetError());
  return properties;
}
void UnlockProperties(SDL_PropertiesID properties) noexcept { SDL_UnlockProperties(properties); }
auto ObserveHint(char const* name, SDL_HintCallback callback, void* context) -> HintRegistration {
  if (!SDL_AddHintCallback(name, callback, context)) throw std::runtime_error(SDL_GetError());
  return {name, callback, context};
}
void ForgetHint(HintRegistration const& registration) noexcept { std::apply(SDL_RemoveHintCallback, registration); }
auto AttachTouch(SDL_TouchID touch) -> SDL_TouchID {
  if (SDL_AddTouch(touch, SDL_TOUCH_DEVICE_DIRECT, "RDP touch") < 0) throw std::runtime_error(SDL_GetError());
  return touch;
}
void DetachTouch(SDL_TouchID touch) noexcept { SDL_DelTouch(touch); }
auto LoadFile(std::filesystem::path const& path) -> char* {
  auto* const text = static_cast<char*>(SDL_LoadFile(path.string().c_str(), nullptr));
  if (!text) throw std::runtime_error("Could not read RDP settings file " + path.string());
  return text;
}
}
