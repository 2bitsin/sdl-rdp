#include "../SDL3.so/rdp/SDL_rdprefresh.hpp"
#include <gtest/gtest.h>

TEST(Refresh, ComparesExactRationalRatesBeyondFloatPrecision) {
  EXPECT_TRUE(rdp::SameRefresh(60, 1, 60000, 1000));
  EXPECT_FALSE(rdp::SameRefresh(16777216, 1000, 16777217, 1000));
  EXPECT_FALSE(rdp::SameRefresh(60, 0, 60000, 1000));
}
