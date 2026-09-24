#include "_detail/pixel-band.hpp"

namespace Backend {
PixelBand::PixelBand(sdlrdp_rect value, std::span<BYTE> bytes) noexcept : _area{ value }, _pixels{ bytes } { }
auto PixelBand::Area() const noexcept -> sdlrdp_rect {
  return _area;
}
auto PixelBand::Pixels() const noexcept -> std::span<BYTE> {
  return _pixels;
}
}
