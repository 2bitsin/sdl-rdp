#pragma once
#include "_detail/check.hpp"

inline bool PrintInput(const SDL_Event& event, SDL_Window* window)
{
    if (event.type == SDL_EVENT_TEXT_INPUT) {
        SDL_Log("event TEXT_INPUT text=%s", event.text.text);
        return true;
    }
    const char* name;
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN: name = "FINGER_DOWN"; break;
    case SDL_EVENT_FINGER_MOTION: name = "FINGER_MOTION"; break;
    case SDL_EVENT_FINGER_UP: name = "FINGER_UP"; break;
    case SDL_EVENT_FINGER_CANCELED: name = "FINGER_CANCELED"; break;
    default: return false;
    }
    int width, height;
    Check(SDL_GetWindowSize(window, &width, &height));
    SDL_Log("event %s id=%llu x=%.3f y=%.3f pressure=%.3f window_x=%.0f window_y=%.0f",
            name, (unsigned long long)event.tfinger.fingerID, event.tfinger.x, event.tfinger.y,
            event.tfinger.pressure, event.tfinger.x * width, event.tfinger.y * height);
    return true;
}

inline void InputMode(const SDL_Event& event, SDL_Window* window)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    switch (event.key.scancode) {
    case SDL_SCANCODE_F6:
        Check(SDL_SetWindowSize(window, 1920, 1080));
        break;
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
