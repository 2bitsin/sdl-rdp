#pragma once
#include "locked-reporter.hpp"

#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <benchmark/benchmark.h>
#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sdl_rdp::integration::support_bench::detail::measurement {
using Duration = Backend::Stopwatch::Clock::duration;

// One session's outcome: the rig's gtest results, counters, label and the measured span, reported once.
class Measurement {
public:
  // An exception from the step ends the session as a fatal failure with no location, as gtest reports one.
  auto Contained(std::string_view phase, std::invocable auto&& step) -> bool {
    auto const ran = [&step] {
      std::invoke(step);
      return true;
    };
    return Backend::Contained(false, ran, [phase](std::string_view text) { FailFatally(phase, text); });
  }
  auto Holds(std::invocable auto&&... steps) -> bool {
    return ((Contained("a rig step", steps) && Healthy()) && ...);
  }
  auto Healthy() const                               -> bool;
  auto Failures() const                              -> std::string;
  auto Measure(Duration span)                        -> void;
  auto MeasureBody(Duration span)                    -> void;
  auto Record(std::string const& name, double value) -> void;
  auto Label(std::string text)                       -> void;
  auto Report(benchmark::State& state) const         -> void;

private:
  static auto FailFatally(std::string_view phase, std::string_view text) -> void;
  auto        Skipped() const                                            -> std::optional<std::string>;

  LockedReporter          _reporter;
  benchmark::UserCounters _counters;
  std::string             _label;
  std::optional<Duration> _span;
  std::optional<Duration> _body;
};
}

namespace sdl_rdp::integration::support_bench {
using detail::measurement::Duration;
using detail::measurement::Measurement;
}
