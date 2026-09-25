#pragma once
#include <sdl-rdp/abi/backend.h>

#include <vector>

namespace sdl_rdp::utilities::detail::region {
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

namespace sdl_rdp::utilities {
using detail::region::Region;
}
