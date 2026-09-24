#pragma once
#include "extent.hpp"
#include "sdl-rdp-backend.h"

#include <cstdint>
#include <limits>

namespace Backend {
// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
inline constexpr unsigned MaximumPictureWidth  = std::numeric_limits<uint16_t>::max() / PixelBytes;
inline constexpr unsigned MaximumPictureHeight = std::numeric_limits<uint16_t>::max();
class PictureGeometry {
public:
              PictureGeometry(Extent size, sdlrdp_aspect aspect);
  sdlrdp_rect Desktop() const;
  sdlrdp_rect Desktop(Extent size) const;
  sdlrdp_rect Bounds() const noexcept;
  bool        Resize(Extent size);
  void        SetAspect(sdlrdp_aspect value);

private:
  Extent        _size;
  sdlrdp_aspect _aspect;
};
}
