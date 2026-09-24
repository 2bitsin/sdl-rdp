#pragma once
#include <SDL3/SDL.h>
#include <cstdlib>

inline auto Check(bool result) -> void {
  if (!result) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
    std::exit(1);
  }
}
