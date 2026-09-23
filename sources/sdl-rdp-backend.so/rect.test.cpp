#include "_detail/rect.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(Intersect, OverlapContainmentAndEmpty)
{
  auto overlap = Backend::Intersect({ -10, -20, 40, 60 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(overlap.has_value());
  EXPECT_EQ(overlap->x, 0);
  EXPECT_EQ(overlap->y, 0);
  EXPECT_EQ(overlap->w, 30);
  EXPECT_EQ(overlap->h, 40);
  auto contained = Backend::Intersect({ 0, 0, 640, 480 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(contained.has_value());
  EXPECT_EQ(contained->w, 320);
  EXPECT_EQ(contained->h, 200);
  EXPECT_FALSE(Backend::Intersect({ 320, 0, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 200, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 400, 300, 40, 30 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 0, 0, 20 }, { 0, 0, 320, 200 }));
  auto edge = Backend::Intersect({ std::numeric_limits<int>::max(), 0, 1, 1 },
                                 { std::numeric_limits<int>::max(), 0, 1, 1 });
  ASSERT_TRUE(edge.has_value());
  EXPECT_EQ(edge->w, 1);
}
TEST(Region, BridgeAndCap)
{
  Backend::Region region;
  region.Add({ 0, 0, 8, 8 });
  region.Add({ 16, 0, 8, 8 });
  ASSERT_EQ(region.rects.size(), 2u);
  region.Add({ 8, 0, 8, 8 });
  ASSERT_EQ(region.rects.size(), 1u);
  EXPECT_EQ(region.rects[0].w, 24);
  region.clear();
  for (int i = 0; i < 16; ++i) region.Add({ i * 20, i * 20, 8, 8 });
  ASSERT_EQ(region.rects.size(), 16u);
  region.Add({ 320, 320, 8, 8 });
  ASSERT_EQ(region.rects.size(), 1u);
  EXPECT_EQ(region.rects[0].w, 328);
  EXPECT_EQ(region.rects[0].h, 328);
}
