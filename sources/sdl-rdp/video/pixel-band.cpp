#include <sdl-rdp/video/pixel-band.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <cstdint>

namespace sdl_rdp::video::detail::pixel_band {
using sdl_rdp::utilities::Rect;
PixelBand::PixelBand(Rect value, std::span<std::uint8_t> bytes) noexcept : _area{ value }, _pixels{ bytes } { }
auto PixelBand::Area() const noexcept -> Rect {
  return _area;
}
auto PixelBand::Pixels() const noexcept -> std::span<std::uint8_t> {
  return _pixels;
}
}
