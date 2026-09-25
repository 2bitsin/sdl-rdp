#include <sdl-rdp/picture/geometry.hpp>

#include <sdl-rdp/picture/exceptions.hpp>

#include <gtest/gtest.h>
#include <array>

namespace sdl_rdp::picture::detail::geometry {
using sdl_rdp::utilities::OutOfRange;
using sdl_rdp::utilities::Rect;

TEST(Dimensions, AcceptsTheWholeRdpRange) {
  auto const size = Dimensions(MaximumPictureWidth, MaximumPictureHeight);
  EXPECT_EQ(size.width, MaximumPictureWidth);
  EXPECT_EQ(size.height, MaximumPictureHeight);
}
TEST(Dimensions, RefusesZeroAndOversize) {
  EXPECT_THROW(Dimensions(0, 1), OutOfRange);
  EXPECT_THROW(Dimensions(1, 0), OutOfRange);
  EXPECT_THROW(Dimensions(MaximumPictureWidth + 1, 1), OutOfRange);
  EXPECT_THROW(Dimensions(1, MaximumPictureHeight + 1), OutOfRange);
}
TEST(ValidateDamage, AcceptsRectanglesInsideTheFrame) {
  std::array const damage{ Rect{ .x = 0, .y = 0, .w = 640, .h = 480 }, Rect{ .x = 600, .y = 400, .w = 40, .h = 80 } };
  EXPECT_NO_THROW(ValidateDamage(damage, { .width = 640, .height = 480 }));
  EXPECT_NO_THROW(ValidateDamage({ }, { .width = 640, .height = 480 }));
}
TEST(ValidateDamage, RefusesEmptyNegativeOrOverhangingRectangles) {
  Extent const size{ .width = 640, .height = 480 };
  EXPECT_THROW(ValidateDamage(std::array{ Rect{ .x = 0, .y = 0, .w = 0, .h = 1 } }, size), DamageOutOfBounds);
  EXPECT_THROW(ValidateDamage(std::array{ Rect{ .x = -1, .y = 0, .w = 1, .h = 1 } }, size), DamageOutOfBounds);
  EXPECT_THROW(ValidateDamage(std::array{ Rect{ .x = 600, .y = 0, .w = 41, .h = 1 } }, size), DamageOutOfBounds);
}
}
