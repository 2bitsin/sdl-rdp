#include <sdl-rdp/bench/support.bench/measurement.hpp>
#include <sdl-rdp/bench/support.bench/session.hpp>

#include <gtest/gtest.h>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>

namespace sdl_rdp::bench::detail::adapter {
using support_bench::Measurement;
struct PhaseCounts {
  std::size_t bodies    = 0;
  std::size_t teardowns = 0;
};

class ThrowingSetUp final : public support_bench::Session<testing::Test> {
public:
       ThrowingSetUp(Measurement& measurement, PhaseCounts& counts);
  auto TestBody() -> void override;

protected:
  auto SetUp()    -> void override;
  auto TearDown() -> void override;

private:
  std::reference_wrapper<PhaseCounts> _counts;
};

namespace {
// The adapter itself under a probe: a SetUp that throws runs no body, one TearDown, and ends as an error.
auto SetUpExceptionEndsTheSession(benchmark::State& state) -> void {
  for ([[maybe_unused]] auto _ : state) {
    PhaseCounts counts;
    Measurement measurement;
    support_bench::RunSession<ThrowingSetUp>(measurement, counts);
    auto const failures = measurement.Failures();
    if (counts.bodies != 0)
      state.SkipWithError("the body ran after SetUp threw");
    else if (counts.teardowns != 1)
      state.SkipWithError(std::format("TearDown ran {} times", counts.teardowns));
    else if (measurement.Healthy())
      state.SkipWithError("the SetUp exception is not a fatal failure");
    else if (!failures.contains("C++ exception with description \"probe\" thrown in SetUp()"))
      state.SkipWithError(std::format("the exception is not the session's failure: {}", failures));
  }
}
}
BENCHMARK(SetUpExceptionEndsTheSession)->Iterations(1);

ThrowingSetUp::ThrowingSetUp(Measurement& measurement, PhaseCounts& counts) : Session(measurement), _counts(counts) { }
auto ThrowingSetUp::TestBody() -> void {
  ++_counts.get().bodies;
}
auto ThrowingSetUp::SetUp() -> void {
  throw std::runtime_error("probe");
}
auto ThrowingSetUp::TearDown() -> void {
  ++_counts.get().teardowns;
}
}
