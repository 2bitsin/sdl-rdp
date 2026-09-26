#pragma once
#include "checks.hpp"
#include "measurement.hpp"

#include <sdl-rdp/utilities/scoped.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <gtest/gtest.h>
#include <benchmark/benchmark.h>
#include <charconv>
#include <concepts>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace sdl_rdp::integration::support_bench::detail::session {
using sdl_rdp::utilities::RAIIWrap;
using sdl_rdp::utilities::Timed;

// A rig fixture run as one benchmark session: SetUp, the case's body, TearDown, each reported to the measurement.
template <std::derived_from<testing::Test> Fixture>
class Session : public Fixture {
public:
  explicit Session(Measurement& measurement) : _measurement(measurement) { }

  auto Run(std::invocable auto body) -> void {
    Torn const torn(*this);
    if (_set_up) _measurement.get().MeasureBody(Timed([&] { _measurement.get().Contained("the test body", body); }));
  }

protected:
  auto Passes(std::invocable auto&&... steps) -> bool {
    return _measurement.get().Passes(steps...);
  }
  auto Measure(Duration span) -> void {
    _measurement.get().Measure(span);
  }
  auto Record(std::string const& name, double value) -> void {
    _measurement.get().Record(name, value);
  }
  // oxbox #49: a floating-point number_text replaces this parse.
  auto Recorded(std::string const& name, std::string_view text) -> std::optional<double> {
    double     value  { };
    auto const last   = std::to_address(text.end());
    auto const parsed = std::from_chars(std::to_address(text.begin()), last, value);
    if (parsed.ec != std::errc{ } || parsed.ptr != last) {
      Fail(std::format("{} is a number: '{}'", name, text));
      return std::nullopt;
    }
    Record(name, value);
    return value;
  }
  auto Label(std::string text) -> void {
    _measurement.get().Label(std::move(text));
  }

private:
  static auto MeasuredSetUp(Session& session) -> Session& {
    session._set_up = session._measurement.get().Contained("SetUp()", [&session] { session.SetUp(); })
                      && session._measurement.get().Healthy();
    return session;
  }
  static auto MeasuredTearDown(Session& session) -> void {
    session._measurement.get().Contained("TearDown()", [&session] { session.TearDown(); });
  }
  using Torn = RAIIWrap<Session&, &Session::MeasuredSetUp, &Session::MeasuredTearDown>;

  std::reference_wrapper<Measurement> _measurement;
  bool                                _set_up      = false;
};

// The measurement outlives the case, so the fixture's construction and destruction report into it too.
template <typename Case, typename... ArgTys>
  requires std::constructible_from<Case, Measurement&, ArgTys&...>
auto RunSession(Measurement& measurement, ArgTys&... arguments) -> void {
  measurement.Contained("the fixture", [&] {
    Case measured(measurement, arguments...);
    measured.Run([&measured] { measured.TestBody(); });
  });
}
template <std::constructible_from<Measurement&> Case>
auto Measured(benchmark::State& state) -> void {
  for (auto _ : state) {
    Measurement measurement;
    RunSession<Case>(measurement);
    measurement.Report(state);
  }
}

// google-benchmark's Apply passes its registration by raw pointer.
auto OneSession(benchmark::Benchmark* bench) -> void;
}

namespace sdl_rdp::integration::support_bench {
using detail::session::Measured;
using detail::session::OneSession;
using detail::session::RunSession;
using detail::session::Session;
}
