#pragma once
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <vector>

namespace Backend {
class Region {
public:
  auto Add(sdlrdp_rect area)        -> void;
  auto Clear() noexcept             -> void;
  auto Swap(Region& other) noexcept -> void;
  auto Rects() const                -> std::vector<sdlrdp_rect> const&;

private:
  std::vector<sdlrdp_rect> rects;
};
}
