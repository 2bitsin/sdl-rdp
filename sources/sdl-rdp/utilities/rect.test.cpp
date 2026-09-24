#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/utilities/region.hpp>

#include <gtest/gtest.h>
#include <limits>
#include <tuple>

namespace {
auto ThenOverlapBounds(sdlrdp_rect const& bounds) -> void {
  EXPECT_EQ(bounds.x, 0);
  EXPECT_EQ(bounds.y, 0);
  EXPECT_EQ(bounds.w, 30);
  EXPECT_EQ(bounds.h, 40);
}
auto ThenOverlap() -> void {
  auto overlap = Backend::Intersect({ -10, -20, 40, 60 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(overlap.has_value());
  Backend::Expects(overlap.has_value(), "intersection exists before inspecting its bounds");
  if (!overlap.has_value()) return;
  ThenOverlapBounds(*overlap);
}
auto ThenContainment() -> void {
  auto contained = Backend::Intersect({ 0, 0, 640, 480 }, { 0, 0, 320, 200 });
  ASSERT_TRUE(contained.has_value());
  Backend::Expects(contained.has_value(), "intersection exists before inspecting its bounds");
  if (!contained.has_value()) return;
  EXPECT_EQ(contained->w, 320);
  EXPECT_EQ(contained->h, 200);
}
auto ThenEmptyIntersections() -> void {
  EXPECT_FALSE(Backend::Intersect({ 320, 0, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 200, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 400, 300, 40, 30 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Backend::Intersect({ 0, 0, 0, 20 }, { 0, 0, 320, 200 }));
}
auto ThenMaximumCoordinate() -> void {
  auto edge = Backend::Intersect({ std::numeric_limits<int>::max(), 0, 1, 1 },
                                 { std::numeric_limits<int>::max(), 0, 1, 1 });
  ASSERT_TRUE(edge.has_value());
  Backend::Expects(edge.has_value(), "intersection exists before inspecting its bounds");
  if (!edge.has_value()) return;
  EXPECT_EQ(edge->w, 1);
}
auto ThenBridge(Backend::Region& region) -> void {
  region.Add({ 0, 0, 8, 8 });
  region.Add({ 16, 0, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 2u);
  region.Add({ 8, 0, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 1u);
  EXPECT_EQ(region.Rects()[0].w, 24);
}
auto ThenRegionCap(Backend::Region& region) -> void {
  region.Clear();
  for (int i = 0; i < 16; ++i) region.Add({ i * 20, i * 20, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 16u);
  region.Add({ 320, 320, 8, 8 });
  ASSERT_EQ(region.Rects().size(), 1u);
  EXPECT_EQ(region.Rects()[0].w, 328);
  EXPECT_EQ(region.Rects()[0].h, 328);
}
}
TEST(Intersect, OverlapContainmentAndEmpty) {
  ASSERT_NO_FATAL_FAILURE(ThenOverlap());
  ASSERT_NO_FATAL_FAILURE(ThenContainment());
  ASSERT_NO_FATAL_FAILURE(ThenEmptyIntersections());
  ThenMaximumCoordinate();
}
TEST(Region, BridgeAndCap) {
  Backend::Region region;
  ASSERT_NO_FATAL_FAILURE(ThenBridge(region));
  ThenRegionCap(region);
}
TEST(Rect, UnionCoversBoth) {
  EXPECT_TRUE(Backend::SameSize(Backend::Union({ 0, 0, 10, 10 }, { 20, 5, 5, 20 }), { 0, 0, 25, 25 }));
  EXPECT_EQ(Backend::Union({ 5, 5, 1, 1 }, { 0, 0, 2, 2 }).x, 0);
}
TEST(Rect, TouchesIncludesSharedEdges) {
  EXPECT_TRUE(Backend::Touches({ 0, 0, 10, 10 }, { 10, 0, 5, 5 }));
  EXPECT_FALSE(Backend::Touches({ 0, 0, 10, 10 }, { 11, 0, 5, 5 }));
}
TEST(RectDeathTest, UnionAndTouchesRejectANegativeExtent) {
  EXPECT_DEATH(std::ignore = Backend::Union({ 0, 0, -1, 1 }, { 0, 0, 1, 1 }), "rectangle width is nonnegative");
  EXPECT_DEATH(std::ignore = Backend::Touches({ 0, 0, 1, 1 }, { 0, 0, 1, -1 }), "rectangle height is nonnegative");
}
TEST(Rect, BytesCountFourOctetsPerPixel) {
  EXPECT_EQ(Backend::RowBytes(320), 1280u);
  EXPECT_EQ(Backend::AreaBytes({ 7, 9, 320, 200 }), 256000u);
}
TEST(RectDeathTest, BytesRejectANegativeExtent) {
  EXPECT_DEATH(std::ignore = Backend::RowBytes(-1), "fits the narrower type");
  EXPECT_DEATH(std::ignore = Backend::AreaBytes({ 0, 0, 1, -1 }), "fits the narrower type");
}
