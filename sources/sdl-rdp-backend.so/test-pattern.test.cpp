#include "_detail/test-pattern.hpp"

#include <algorithm>
#include <array>

namespace Headless {
auto MovingTilePattern(std::span<std::uint32_t> pixels, std::size_t width, std::size_t height, std::size_t frame)
    -> void {
  constexpr std::array<std::uint32_t, 4> colors { 0x335577, 0x55aaff, 0x779933, 0xaa5533 };
  auto const                             shift  = (frame * 8) % width;
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x)
      pixels[(y * width) + x] = colors[(((x + width - shift) % width) / 64 + y / 64) % colors.size()];
}
auto NoisePattern(std::span<std::uint32_t> pixels, std::uint32_t value) -> void {
  std::ranges::generate(pixels, [&] {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value & 0xffffff;
  });
}
auto GraphicsScene(std::uint32_t frame, bool noise) -> std::vector<std::uint32_t> {
  std::vector<std::uint32_t> pixels(640uz * 480, 0x00010101);
  auto const                 left   = std::size_t{ frame % 640 };
  auto const                 width  = std::min(left + 32, 640uz) - left;
  if (noise)
    NoisePattern(pixels, frame + 1);
  else
    for (std::size_t y = 40; y < 72; ++y)
      std::ranges::fill(std::span(pixels).subspan((y * 640) + left, width), 0x0000ff00);
  return pixels;
}
}
