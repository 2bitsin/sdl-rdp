#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Headless {
auto MovingTilePattern(std::span<std::uint32_t> pixels, std::size_t width, std::size_t height, std::size_t frame)
    -> void;
auto NoisePattern(std::span<std::uint32_t> pixels, std::uint32_t value) -> void;
auto GraphicsScene(std::uint32_t frame, bool noise)                     -> std::vector<std::uint32_t>;
}
