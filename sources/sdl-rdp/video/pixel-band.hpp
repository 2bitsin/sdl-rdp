#pragma once
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <cstdint>
#include <span>

namespace Backend {
class PixelBand {
public:
       PixelBand(sdlrdp_rect value, std::span<std::uint8_t> bytes) noexcept;
  auto Area() const noexcept   -> sdlrdp_rect;
  auto Pixels() const noexcept -> std::span<std::uint8_t>;

private:
  sdlrdp_rect             _area;
  std::span<std::uint8_t> _pixels;
};
}
