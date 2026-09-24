#pragma once
#include "sdl-rdp-backend.h"

#include <winpr/wtypes.h>
#include <span>

namespace Backend {
class PixelBand {
public:
       PixelBand(sdlrdp_rect value, std::span<BYTE> bytes) noexcept;
  auto Area() const noexcept   -> sdlrdp_rect;
  auto Pixels() const noexcept -> std::span<BYTE>;

private:
  sdlrdp_rect     _area;
  std::span<BYTE> _pixels;
};
}
