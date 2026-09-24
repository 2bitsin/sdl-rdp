#include "_detail/pixel-band.hpp"

namespace Backend {
PixelBand::PixelBand(sdlrdp_rect value, std::span<BYTE> bytes) noexcept : _area{ value }, _pixels{ bytes } { }
sdlrdp_rect PixelBand::Area() const noexcept {
  return _area;
}
std::span<BYTE> PixelBand::Pixels() const noexcept {
  return _pixels;
}
}
