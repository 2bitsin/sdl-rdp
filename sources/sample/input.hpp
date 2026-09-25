#pragma once
#include <sample/check.hpp>

#include <cstdint>

namespace sample::detail::input {
inline auto TouchName(std::uint32_t type) -> char const* {
  switch (type) {
  case SDL_EVENT_FINGER_DOWN:     return "FINGER_DOWN";
  case SDL_EVENT_FINGER_MOTION:   return "FINGER_MOTION";
  case SDL_EVENT_FINGER_UP:       return "FINGER_UP";
  case SDL_EVENT_FINGER_CANCELED: return "FINGER_CANCELED";
  default:                        return nullptr;
  }
}
inline auto PrintInput(SDL_Event const& event, SDL_Window* window) -> bool {
  if (event.type == SDL_EVENT_TEXT_INPUT) {
    SDL_Log("event TEXT_INPUT text=%s", event.text.text);
    return true;
  }
  char const* name = TouchName(event.type);
  if (!name) return false;
  int width  = 0;
  int height = 0;
  Check(SDL_GetWindowSize(window, &width, &height));
  SDL_Log("event %s id=%" SDL_PRIu64 " x=%.3f y=%.3f pressure=%.3f window_x=%.0f window_y=%.0f", name,
          event.tfinger.fingerID, event.tfinger.x, event.tfinger.y, event.tfinger.pressure,
          event.tfinger.x * static_cast<float>(width), event.tfinger.y * static_cast<float>(height));
  return true;
}

inline auto InputMode(SDL_Event const& event, SDL_Window* window) -> void {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
  switch (event.key.scancode) {
  case SDL_SCANCODE_F6: Check(SDL_SetWindowSize(window, 1920, 1080)); break;
  case SDL_SCANCODE_F2:
    Check(SDL_TextInputActive(window) ? SDL_StopTextInput(window) : SDL_StartTextInput(window));
    SDL_Log("event TEXT_MODE active=%d", SDL_TextInputActive(window));
    break;
  case SDL_SCANCODE_F3:
    Check(SDL_SetWindowRelativeMouseMode(window, !SDL_GetWindowRelativeMouseMode(window)));
    SDL_Log("event RELATIVE_MODE active=%d", SDL_GetWindowRelativeMouseMode(window));
    break;
  default: break;
  }
}
}

namespace sample {
using detail::input::InputMode;
using detail::input::PrintInput;
using detail::input::TouchName;
}
