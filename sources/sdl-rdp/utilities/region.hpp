#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <vector>

namespace sdl_rdp::utilities::detail::region {
using sdl_rdp::utilities::Rect;
class Region {
public:
  auto Add(Rect area)               -> void;
  auto Clear() noexcept             -> void;
  auto Swap(Region& other) noexcept -> void;
  auto Rects() const                -> std::vector<Rect> const&;

private:
  std::vector<Rect> rects;
};
}

namespace sdl_rdp::utilities {
using detail::region::Region;
}
