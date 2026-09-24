#pragma once
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <mutex>
#include <vector>

namespace sdl_rdp::bench::support_bench::detail::locked_reporter {
// Intercepts gtest results on every thread for its lifetime; construct and destroy it while no rig thread runs.
class LockedReporter final : public testing::ScopedFakeTestPartResultReporter {
public:
       LockedReporter();
       LockedReporter(LockedReporter const&)               = delete;
       LockedReporter(LockedReporter&&)                    = delete;
       ~LockedReporter()                                   override;
  auto operator=(LockedReporter const&) -> LockedReporter& = delete;
  auto operator=(LockedReporter&&)      -> LockedReporter& = delete;

  auto ReportTestPartResult(testing::TestPartResult const& result) -> void override;
  auto Snapshot() const                                            -> std::vector<testing::TestPartResult>;

private:
  mutable std::mutex                   _guard;
  std::vector<testing::TestPartResult> _results;
};
}

namespace sdl_rdp::bench::support_bench {
using detail::locked_reporter::LockedReporter;
}
