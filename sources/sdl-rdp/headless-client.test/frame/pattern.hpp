#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::pattern {
auto MovingTilePattern(std::span<std::uint32_t> pixels, std::size_t width, std::size_t height, std::size_t frame)
    -> void;
auto NoisePattern(std::span<std::uint32_t> pixels, std::uint32_t value)    -> void;
auto HashPattern(std::span<std::uint32_t> pixels, std::uint32_t first = 0) -> void;
auto GraphicsScene(std::uint32_t frame, bool noise)                        -> std::vector<std::uint32_t>;
}

namespace sdl_rdp::headless_client_test::frame {
using detail::pattern::GraphicsScene;
using detail::pattern::HashPattern;
using detail::pattern::MovingTilePattern;
using detail::pattern::NoisePattern;
}
