#include <sdl-rdp/utilities/geometry.hpp>

#include <sdl-rdp/utilities/region.hpp>

#include <gtest/gtest.h>
#include <limits>
#include <tuple>

namespace sdl_rdp::utilities::detail::geometry {
namespace {
auto ThenOverlapBounds(Rect const& bounds) -> void {
  EXPECT_EQ(bounds.x, 0);
  EXPECT_EQ(bounds.y, 0);
  EXPECT_EQ(bounds.w, 30);
  EXPECT_EQ(bounds.h, 40);
}
auto ThenOverlap() -> void {
  auto overlap = Intersect({ .x = -10, .y = -20, .w = 40, .h = 60 }, { .x = 0, .y = 0, .w = 320, .h = 200 });
  ASSERT_TRUE(overlap.has_value());
  Expects(overlap.has_value(), "intersection exists before inspecting its bounds");
  if (!overlap.has_value()) return;
  ThenOverlapBounds(*overlap);
}
auto ThenContainment() -> void {
  auto contained = Intersect({ .x = 0, .y = 0, .w = 640, .h = 480 }, { .x = 0, .y = 0, .w = 320, .h = 200 });
  ASSERT_TRUE(contained.has_value());
  Expects(contained.has_value(), "intersection exists before inspecting its bounds");
  if (!contained.has_value()) return;
  EXPECT_EQ(contained->w, 320);
  EXPECT_EQ(contained->h, 200);
}
auto ThenEmptyIntersections() -> void {
  EXPECT_FALSE(Intersect({ 320, 0, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Intersect({ 0, 200, 10, 20 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Intersect({ 400, 300, 40, 30 }, { 0, 0, 320, 200 }));
  EXPECT_FALSE(Intersect({ 0, 0, 0, 20 }, { 0, 0, 320, 200 }));
}
auto ThenMaximumCoordinate() -> void {
  auto edge = Intersect({ .x = std::numeric_limits<int>::max(), .y = 0, .w = 1, .h = 1 },
                        { .x = std::numeric_limits<int>::max(), .y = 0, .w = 1, .h = 1 });
  ASSERT_TRUE(edge.has_value());
  Expects(edge.has_value(), "intersection exists before inspecting its bounds");
  if (!edge.has_value()) return;
  EXPECT_EQ(edge->w, 1);
}
auto ThenBridge(Region& region) -> void {
  region.Add({ .x = 0, .y = 0, .w = 8, .h = 8 });
  region.Add({ .x = 16, .y = 0, .w = 8, .h = 8 });
  ASSERT_EQ(region.Rects().size(), 2u);
  region.Add({ .x = 8, .y = 0, .w = 8, .h = 8 });
  ASSERT_EQ(region.Rects().size(), 1u);
  EXPECT_EQ(region.Rects()[0].w, 24);
}
auto ThenRegionCap(Region& region) -> void {
  region.Clear();
  for (int i = 0; i < 16; ++i) region.Add({ .x = i * 20, .y = i * 20, .w = 8, .h = 8 });
  ASSERT_EQ(region.Rects().size(), 16u);
  region.Add({ .x = 320, .y = 320, .w = 8, .h = 8 });
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
  Region region;
  ASSERT_NO_FATAL_FAILURE(ThenBridge(region));
  ThenRegionCap(region);
}
TEST(Rect, UnionCoversBoth) {
  EXPECT_TRUE(SameSize(Union({ 0, 0, 10, 10 }, { 20, 5, 5, 20 }), { 0, 0, 25, 25 }));
  EXPECT_EQ(Union({ 5, 5, 1, 1 }, { 0, 0, 2, 2 }).x, 0);
}
TEST(Rect, TouchesIncludesSharedEdges) {
  EXPECT_TRUE(Touches({ 0, 0, 10, 10 }, { 10, 0, 5, 5 }));
  EXPECT_FALSE(Touches({ 0, 0, 10, 10 }, { 11, 0, 5, 5 }));
}
TEST(RectDeathTest, UnionAndTouchesRejectANegativeExtent) {
  EXPECT_DEATH(std::ignore = Union({ 0, 0, -1, 1 }, { 0, 0, 1, 1 }), "rectangle width is nonnegative");
  EXPECT_DEATH(std::ignore = Touches({ 0, 0, 1, 1 }, { 0, 0, 1, -1 }), "rectangle height is nonnegative");
}
TEST(Rect, BytesCountFourOctetsPerPixel) {
  EXPECT_EQ(RowBytes(320), 1280u);
  EXPECT_EQ(Stride(320), 1280u);
  EXPECT_EQ(AreaBytes({ 7, 9, 320, 200 }), 256000u);
}
TEST(RectDeathTest, BytesRejectANegativeExtent) {
  EXPECT_DEATH(std::ignore = RowBytes(-1), "fits the narrower type");
  EXPECT_DEATH(std::ignore = AreaBytes({ 0, 0, 1, -1 }), "fits the narrower type");
}
}
