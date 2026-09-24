#include <sdl-rdp/utilities/deadline.hpp>

#include <gtest/gtest.h>

TEST(AbiDeadline, NegativeTimeoutNeverExpires) {
  EXPECT_EQ(Backend::AbiDeadline(-1), Backend::Deadline::max());
}
TEST(AbiDeadline, TimeoutCountsFromNow) {
  auto const before   = std::chrono::steady_clock::now();
  auto const deadline = Backend::AbiDeadline(250);
  EXPECT_GE(deadline, before + std::chrono::milliseconds(250));
  EXPECT_LE(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
}
TEST(DeadlineAfter, ZeroIsNow) {
  auto const before = std::chrono::steady_clock::now();
  EXPECT_GE(Backend::DeadlineAfter(std::chrono::milliseconds::zero()), before);
}
