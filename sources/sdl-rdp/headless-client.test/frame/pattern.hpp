#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::pattern {
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::utilities::Rect;

auto MovingTilePattern(std::span<std::uint32_t> pixels, std::size_t width, std::size_t height, std::size_t frame)
    -> void;
auto NoisePattern(std::span<std::uint32_t> pixels, std::uint32_t value)                             -> void;
auto HashPattern(std::span<std::uint32_t> pixels, std::uint32_t first = 0)                          -> void;
auto RandomPattern(std::span<std::uint32_t> pixels, std::uint32_t seed)                             -> void;
auto FillArea(std::span<std::uint32_t> pixels, std::size_t stride, Rect area, std::uint32_t colour) -> void;
auto GraphicsScene(std::uint32_t frame, bool noise)                                                 -> Pixels;
}

namespace sdl_rdp::headless_client_test::frame {
using detail::pattern::FillArea;
using detail::pattern::GraphicsScene;
using detail::pattern::HashPattern;
using detail::pattern::MovingTilePattern;
using detail::pattern::NoisePattern;
using detail::pattern::RandomPattern;
}
