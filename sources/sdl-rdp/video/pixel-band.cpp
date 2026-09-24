#include <sdl-rdp/video/pixel-band.hpp>
#include <cstdint>

namespace Backend {
PixelBand::PixelBand(sdlrdp_rect value, std::span<std::uint8_t> bytes) noexcept : _area{ value }, _pixels{ bytes } { }
auto PixelBand::Area() const noexcept -> sdlrdp_rect {
  return _area;
}
auto PixelBand::Pixels() const noexcept -> std::span<std::uint8_t> {
  return _pixels;
}
}
