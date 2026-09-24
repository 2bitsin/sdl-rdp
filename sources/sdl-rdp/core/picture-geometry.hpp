#pragma once
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <cstdint>
#include <limits>

namespace Backend {
// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
inline constexpr unsigned MaximumPictureWidth  = std::numeric_limits<uint16_t>::max() / PixelBytes;
inline constexpr unsigned MaximumPictureHeight = std::numeric_limits<uint16_t>::max();
class PictureGeometry {
public:
       PictureGeometry(Extent size, sdlrdp_aspect aspect);
  auto Desktop() const                -> sdlrdp_rect;
  auto Desktop(Extent size) const     -> sdlrdp_rect;
  auto Bounds() const noexcept        -> sdlrdp_rect;
  auto Resize(Extent size)            -> bool;
  auto SetAspect(sdlrdp_aspect value) -> void;

private:
  Extent        _size;
  sdlrdp_aspect _aspect;
};
}

namespace Backend::Avc {
auto Aligned(unsigned dimension) -> unsigned;
}
