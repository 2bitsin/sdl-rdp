#pragma once
#include <array>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>

namespace Headless {
inline void MovingTilePattern(std::span<std::uint32_t> pixels, unsigned width, unsigned height, unsigned frame)
{
  constexpr std::array<std::uint32_t, 4> colors{ 0x335577, 0x55aaff, 0x779933, 0xaa5533 };
  auto                                   shift  = (std::uint64_t(frame) * 8) % width;
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x)
      pixels[(y * width) + x] = colors[(((x + width - shift) % width) / 64 + y / 64) % colors.size()];
}
inline std::vector<std::uint32_t> GraphicsScene(unsigned frame, bool noise)
{
  std::vector<std::uint32_t> pixels(640uz * 480, 0x00010101);
  if (noise) {
    std::uint32_t value = frame + 1;
    for (auto& pixel : pixels) {
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
      pixel = value & 0xffffff;
    }
  } else
    for (unsigned y = 40; y < 72; ++y)
      for (unsigned x = frame % 640; x < std::min((frame % 640) + 32, 640u); ++x)
        pixels[(y * 640) + x] = 0x0000ff00;
  return pixels;
}
}
