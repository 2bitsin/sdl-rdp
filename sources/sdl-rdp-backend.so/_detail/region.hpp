#pragma once
#include "sdl-rdp-backend.h"

#include <vector>

namespace Backend {
class Region {
public:
  auto Add(sdlrdp_rect area) -> void;
  auto empty() const         -> bool;
  auto clear()               -> void;
  auto Rects() const         -> std::vector<sdlrdp_rect> const&;
  auto Swap(Region& other)   -> void;

private:
  std::vector<sdlrdp_rect> rects;
};
}
