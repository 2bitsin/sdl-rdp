#include <sdl-rdp/headless-client.test/frame/pattern.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace sdl_rdp::headless_client_test::frame::detail::pattern {
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::ExpectsArea;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rows;

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
// Knuth's multiplicative hash: every pixel differs from its neighbours, so no codec finds runs.
auto HashPattern(std::span<std::uint32_t> pixels, std::uint32_t first) -> void {
  std::ranges::generate(pixels, [index = first]() mutable { return (index++ * 2654435761u) & 0xffffff; });
}
auto RandomPattern(std::span<std::uint32_t> pixels, std::uint32_t seed) -> void {
  std::mt19937 random(seed);  // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
}
auto FillArea(std::span<std::uint32_t> pixels, std::size_t stride, Rect area, std::uint32_t colour) -> void {
  ExpectsArea(area);
  auto const right  = Narrowed<std::size_t>(area.x + area.w);
  auto const bottom = Narrowed<std::size_t>(area.y + area.h);
  Expects(right <= stride, "the area fits the stride");
  Expects(bottom * stride <= pixels.size(), "the area fits the pixels");
  std::ranges::for_each(Rows(area), [&](Rect row) {
    auto const first = (Narrowed<std::size_t>(row.y) * stride) + Narrowed<std::size_t>(row.x);
    std::ranges::fill(pixels.subspan(first, Narrowed<std::size_t>(row.w)), colour);
  });
}
auto GraphicsScene(std::uint32_t frame, bool noise) -> Pixels {
  Pixels     pixels(640uz * 480, 0x00010101);
  auto const left   = Narrowed<int>(frame % 640);
  if (noise)
    NoisePattern(pixels, frame + 1);
  else
    FillArea(pixels, 640, { .x = left, .y = 40, .w = std::min(left + 32, 640) - left, .h = 32 }, 0x0000ff00);
  return pixels;
}
}
