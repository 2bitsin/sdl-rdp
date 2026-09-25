#include <sdl-rdp/utilities/deadline.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::utilities::detail::deadline {
TEST(AbiDeadline, NegativeTimeoutNeverExpires) {
  EXPECT_EQ(AbiDeadline(-1), Deadline::max());
}
TEST(AbiDeadline, TimeoutCountsFromNow) {
  auto const before   = std::chrono::steady_clock::now();
  auto const deadline = AbiDeadline(250);
  EXPECT_GE(deadline, before + std::chrono::milliseconds(250));
  EXPECT_LE(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
}
TEST(DeadlineAfter, ZeroIsNow) {
  auto const before = std::chrono::steady_clock::now();
  EXPECT_GE(DeadlineAfter(std::chrono::milliseconds::zero()), before);
}
TEST(Until, StopsWhenReady) {
  int  waits = 0;
  auto ready = [&waits] { return waits == 3; };
  EXPECT_TRUE(Until(Deadline::max(), [&waits] { return ++waits > 0; }, ready));
  EXPECT_EQ(waits, 3);
}
TEST(Until, AFailedWaitEndsItUnready) {
  int waits = 0;
  EXPECT_FALSE(Until(Deadline::max(), [&waits] { return ++waits < 2; }, [] { return false; }));
  EXPECT_EQ(waits, 2);
}
TEST(Until, APassedDeadlineChecksReadinessOnce) {
  int waits = 0;
  EXPECT_FALSE(Until(Deadline::min(), [&waits] { return ++waits > 0; }, [] { return false; }));
  EXPECT_EQ(waits, 0);
}
TEST(Throughout, RunsTheStepUntilTheDeadline) {
  auto const deadline = DeadlineAfter(std::chrono::milliseconds(20));
  EXPECT_TRUE(Throughout(deadline, Sleeping(std::chrono::milliseconds(1))));
  EXPECT_GE(std::chrono::steady_clock::now(), deadline);
}
TEST(Throughout, AFailedStepEndsItEarly) {
  EXPECT_FALSE(Throughout(Deadline::max(), [] { return false; }));
}
}
