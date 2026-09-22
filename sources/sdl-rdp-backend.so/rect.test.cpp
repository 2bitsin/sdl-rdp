#include "_detail/rect.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(Intersect, OverlapContainmentAndEmpty) {
  auto overlap = Backend::Intersect({-10, -20, 40, 60}, {0, 0, 320, 200});
  ASSERT_TRUE(overlap);
  EXPECT_EQ(overlap->x, 0);
  EXPECT_EQ(overlap->y, 0);
  EXPECT_EQ(overlap->w, 30);
  EXPECT_EQ(overlap->h, 40);
  auto contained = Backend::Intersect({0, 0, 640, 480}, {0, 0, 320, 200});
  ASSERT_TRUE(contained);
  EXPECT_EQ(contained->w, 320);
  EXPECT_EQ(contained->h, 200);
  EXPECT_FALSE(Backend::Intersect({320, 0, 10, 20}, {0, 0, 320, 200}));
  EXPECT_FALSE(Backend::Intersect({0, 200, 10, 20}, {0, 0, 320, 200}));
  EXPECT_FALSE(Backend::Intersect({400, 300, 40, 30}, {0, 0, 320, 200}));
  EXPECT_FALSE(Backend::Intersect({0, 0, 0, 20}, {0, 0, 320, 200}));
  auto edge = Backend::Intersect({std::numeric_limits<int>::max(), 0, 1, 1},
                                {std::numeric_limits<int>::max(), 0, 1, 1});
  ASSERT_TRUE(edge);
  EXPECT_EQ(edge->w, 1);
}
