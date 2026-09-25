#include <sdl-rdp/utilities/stopwatch.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::utilities::detail::stopwatch {
TEST(Stopwatch, LapsPartitionTheElapsedTime) {
  Stopwatch const total;
  Stopwatch       watch;
  auto const      first  = watch.Lap();
  auto const      second = watch.Elapsed();
  EXPECT_GE(first.count(), 0);
  EXPECT_LE(first + second, total.Elapsed());
}
TEST(Timed, MeasuresTheStepItRuns) {
  Stopwatch const total;
  bool            ran     = false;
  auto const      elapsed = Timed([&ran] { ran = true; });
  EXPECT_TRUE(ran);
  EXPECT_LE(elapsed, total.Elapsed());
}
}
