#include "_detail/rect.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace {
void ThenOverlapBounds(sdlrdp_rect const& bounds) {
  EXPECT_EQ(bounds.x, 0);
  EXPECT_EQ(bounds.y, 0);
  EXPECT_EQ(bounds.w, 30);
  EXPECT_EQ(bounds.h, 40);
}
void ThenOverlap() {
  auto overlap = Backend::Intersect({ -10, -20, 40, 60 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(overlap.has_value());
  Backend::Expects(overlap.has_value(), "intersection exists before inspecting its bounds");
  if (!overlap.has_value()) return;
  ThenOverlapBounds(*overlap);
}
void ThenContainment() {
  auto contained = Backend::Intersect({ 0, 0, 640, 480 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(contained.has_value());
  Backend::Expects(contained.has_value(), "intersection exists before inspecting its bounds");
  if (!contained.has_value()) return;
  EXPECT_EQ(contained->w, 320);
  EXPECT_EQ(contained->h, 200);
}
void ThenEmptyIntersections() {
  EXPECT_FALSE(Backend::Intersect({ 320, 0, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 200, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 400, 300, 40, 30 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 0, 0, 20 }, { 0, 0, 320, 200 }));
}
void ThenMaximumCoordinate() {
  auto edge =
      Backend::Intersect({ std::numeric_limits<int>::max(), 0, 1, 1 }, { std::numeric_limits<int>::max(), 0, 1, 1 });
  ASSERT_TRUE(edge.has_value());
  Backend::Expects(edge.has_value(), "intersection exists before inspecting its bounds");
  if (!edge.has_value()) return;
  EXPECT_EQ(edge->w, 1);
}
void ThenBridge(Backend::Region& region) {
  region.Add({ 0, 0, 8, 8 });
  region.Add({ 16, 0, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 2u);
  region.Add({ 8, 0, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 1u);
  EXPECT_EQ(region.Rects()[0].w, 24);
}
void ThenRegionCap(Backend::Region& region) {
  region.clear();
  for (int i = 0; i < 16; ++i)
    region.Add({ i * 20, i * 20, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 16u);
  region.Add({ 320, 320, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 1u);
  EXPECT_EQ(region.Rects()[0].w, 328);
  EXPECT_EQ(region.Rects()[0].h, 328);
}
}
TEST(Intersect, OverlapContainmentAndEmpty) {
  ThenOverlap();
  if (::testing::Test::HasFatalFailure()) return;
  ThenContainment();
  if (::testing::Test::HasFatalFailure()) return;
  ThenEmptyIntersections();
  if (::testing::Test::HasFatalFailure()) return;
  ThenMaximumCoordinate();
}
TEST(Region, BridgeAndCap) {
  Backend::Region region;
  ThenBridge(region);
  if (::testing::Test::HasFatalFailure()) return;
  ThenRegionCap(region);
}
