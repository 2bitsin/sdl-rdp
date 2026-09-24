#include <sdl-rdp/bench/support.bench/locked-reporter.hpp>

namespace sdl_rdp::bench::support_bench::detail::locked_reporter {
// The override receives every result, so the base's array is never written.
LockedReporter::LockedReporter() : ScopedFakeTestPartResultReporter(INTERCEPT_ALL_THREADS, nullptr) { }
LockedReporter::~LockedReporter() = default;
auto LockedReporter::ReportTestPartResult(testing::TestPartResult const& result) -> void {
  std::scoped_lock const lock(_guard);
  _results.push_back(result);
}
auto LockedReporter::Snapshot() const -> std::vector<testing::TestPartResult> {
  std::scoped_lock const lock(_guard);
  return _results;
}
}
