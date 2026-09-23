#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace Headless {
inline void MovingTilePattern(std::span<std::uint32_t> pixels, unsigned width, unsigned height, unsigned frame) {
  constexpr std::array<std::uint32_t, 4> colors{0x335577, 0x55aaff, 0x779933, 0xaa5533};
  auto shift = (std::uint64_t(frame) * 8) % width;
  for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
    pixels[y * width + x] = colors[(((x + width - shift) % width) / 64 + y / 64) % colors.size()];
}
}
