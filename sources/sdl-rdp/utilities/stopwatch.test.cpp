#include <sdl-rdp/utilities/stopwatch.hpp>

#include <gtest/gtest.h>

TEST(Stopwatch, LapsPartitionTheElapsedTime) {
  Backend::Stopwatch const total;
  Backend::Stopwatch       watch;
  auto const               first  = watch.Lap();
  auto const               second = watch.Elapsed();
  EXPECT_GE(first.count(), 0);
  EXPECT_LE(first + second, total.Elapsed());
}
TEST(Timed, MeasuresTheStepItRuns) {
  Backend::Stopwatch const total;
  bool                     ran     = false;
  auto const               elapsed = Backend::Timed([&ran] { ran = true; });
  EXPECT_TRUE(ran);
  EXPECT_LE(elapsed, total.Elapsed());
}
