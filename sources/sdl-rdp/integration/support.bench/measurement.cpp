#include <sdl-rdp/integration/support.bench/measurement.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <ranges>
#include <utility>
#include <vector>

namespace sdl_rdp::integration::support_bench::detail::measurement {
namespace {
auto OneLine(std::string_view text) -> std::string {
  return text | std::views::split('\n') | std::views::filter([](auto line) { return !line.empty(); })
         | std::views::join_with(' ') | std::ranges::to<std::string>();
}
auto Located(testing::TestPartResult const& part) -> std::string {
  auto const* file = part.file_name();
  if (file == nullptr) return OneLine(part.summary());
  return std::format("{}:{}: {}", file, part.line_number(), OneLine(part.summary()));
}
auto Ended(testing::TestPartResult const& part) -> bool {
  return part.fatally_failed() || part.skipped();
}
}
auto Measurement::Healthy() const -> bool {
  return std::ranges::none_of(_reporter.Snapshot(), Ended);
}
auto Measurement::Measure(Duration span) -> void {
  ::utilities::Expects(!_span.has_value(), "a session measures one span");
  _span = span;
}
auto Measurement::MeasureBody(Duration span) -> void {
  _body = span;
}
auto Measurement::_FailFatally(std::string_view phase, std::string_view text) -> void {
  GTEST_FAIL_AT(nullptr, -1) << std::format("C++ exception with description \"{}\" thrown in {}.", text, phase);
}
auto Measurement::Record(std::string const& name, double value) -> void {
  _counters.insert_or_assign(name, benchmark::Counter(value));
}
auto Measurement::Label(std::string text) -> void {
  _label = std::move(text);
}
auto Measurement::Report(benchmark::State& state) const -> void {
  state.counters.insert(_counters.begin(), _counters.end());
  state.SetLabel(_label);
  if (auto const failures = Failures(); !failures.empty())
    state.SkipWithError(failures);
  else if (auto const reason = _Skipped())
    state.SkipWithMessage(*reason);
  else
    state.SetIterationTime(std::chrono::duration<double>(::utilities::Required(_span.or_else([this] { return _body; }),
                                                                               "the session measured a span"))
                               .count());
}
auto Measurement::Failures() const -> std::string {
  return _reporter.Snapshot() | std::views::filter(&testing::TestPartResult::failed) | std::views::transform(Located)
         | std::views::join_with(std::string_view{ "; " }) | std::ranges::to<std::string>();
}
auto Measurement::_Skipped() const -> std::optional<std::string> {
  auto const results = _reporter.Snapshot();
  auto const skipped = std::ranges::find_if(results, &testing::TestPartResult::skipped);
  if (skipped == results.end()) return std::nullopt;
  return OneLine(skipped->summary());
}
}
