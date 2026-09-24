#include "_detail/tone-measurements.hpp"

#include "_detail/contract.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace Headless {
std::pair<double, double> ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) {
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
}
