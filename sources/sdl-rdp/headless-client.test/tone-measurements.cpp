#include <sdl-rdp/headless-client.test/tone-measurements.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <ranges>

namespace Headless {
auto ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) -> std::pair<double, double> {
  using utilities::Expects;
  auto start = std::ranges::find_if(samples, [](auto value) { return std::abs(value) > 100; }) - samples.begin();
  start += start % 2;
  Expects(start < samples.size(), "captured tone contains signal");
  auto frames = (samples.size() - start) / 2;
  Expects(frames > 1, "signal contains at least two frames");
  Expects(rate > 0, "sample rate is positive");
  unsigned crossings = 0;
  double   square    = 0;
  for (std::size_t frame = 1; frame < frames; ++frame) {
    auto sample = samples[start + (frame * 2)];
    crossings += samples[start + ((frame - 1) * 2)] <= 0 && sample > 0;
    square    += double(sample) * sample;
  }
  auto frequency = crossings * double(rate) / double(frames);
  auto db        = 20 * std::log10(std::sqrt(square / double(frames - 1)) * std::numbers::sqrt2 / 32767);
  return { frequency, db };
}
auto MaximumGapMs(std::span<std::chrono::steady_clock::time_point const> received) -> double {
  utilities::Expects(received.size() > 1, "a gap needs two receptions");
  auto const gap = [](auto earlier, auto later) {
    return std::chrono::duration<double, std::milli>(later - earlier).count();
  };
  return std::ranges::max(received | std::views::adjacent_transform<2>(gap));
}
}
