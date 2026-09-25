#pragma once
#include <sdl-rdp/utilities/rect.hpp>

#include <cstdint>
#include <span>

namespace sdl_rdp::video::detail::pixel_band {
using sdl_rdp::utilities::Rect;
class PixelBand {
public:
       PixelBand(Rect value, std::span<std::uint8_t> bytes) noexcept;
  auto Area() const noexcept   -> Rect;
  auto Pixels() const noexcept -> std::span<std::uint8_t>;

private:
  Rect                    _area;
  std::span<std::uint8_t> _pixels;
};
}

namespace sdl_rdp::video {
using detail::pixel_band::PixelBand;
}
