#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace sdl_rdp::headless_client_test::audio::detail::tone_measurements {
auto ToneMeasurements(std::vector<std::int16_t> const& samples, std::uint32_t rate) -> std::pair<double, double>;
auto MaximumGapMs(std::span<std::chrono::steady_clock::time_point const> received)  -> double;
}

namespace sdl_rdp::headless_client_test::audio {
using detail::tone_measurements::MaximumGapMs;
using detail::tone_measurements::ToneMeasurements;
}
