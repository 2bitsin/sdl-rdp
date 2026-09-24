#pragma once
#include "sdl-rdp-backend.h"

#include <span>
#include <winpr/wtypes.h>

namespace Backend {
class PixelBand {
public:
                  PixelBand(sdlrdp_rect value, std::span<BYTE> bytes) noexcept;
  sdlrdp_rect     Area() const                                        noexcept;
  std::span<BYTE> Pixels() const                                      noexcept;

private:
  sdlrdp_rect     _area;
  std::span<BYTE> _pixels;
};
}
