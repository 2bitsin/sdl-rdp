#pragma once
#include <SDL3/SDL.h>
#include <memory>

inline bool PrintClipboardEvent(SDL_Event const& event) {
  if (event.type != SDL_EVENT_CLIPBOARD_UPDATE) return false;
  std::unique_ptr<char, decltype(&SDL_free)> const text(SDL_GetClipboardText(), SDL_free);
  if (text)
    SDL_Log("event CLIPBOARD text=%s", text.get());
  else
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
  return true;
}
