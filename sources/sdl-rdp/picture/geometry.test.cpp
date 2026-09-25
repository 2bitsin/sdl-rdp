#include <sdl-rdp/picture/geometry.hpp>

#include <sdl-rdp/picture/exceptions.hpp>

#include <gtest/gtest.h>
#include <array>

namespace Backend {
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
  std::array const damage{ sdlrdp_rect{ 0, 0, 640, 480 }, sdlrdp_rect{ 600, 400, 40, 80 } };
  EXPECT_NO_THROW(ValidateDamage(damage, { .width = 640, .height = 480 }));
  EXPECT_NO_THROW(ValidateDamage({ }, { .width = 640, .height = 480 }));
}
TEST(ValidateDamage, RefusesEmptyNegativeOrOverhangingRectangles) {
  Extent const size{ .width = 640, .height = 480 };
  EXPECT_THROW(ValidateDamage(std::array{ sdlrdp_rect{ 0, 0, 0, 1 } }, size), sdl_rdp::picture::DamageOutOfBounds);
  EXPECT_THROW(ValidateDamage(std::array{ sdlrdp_rect{ -1, 0, 1, 1 } }, size), sdl_rdp::picture::DamageOutOfBounds);
  EXPECT_THROW(ValidateDamage(std::array{ sdlrdp_rect{ 600, 0, 41, 1 } }, size), sdl_rdp::picture::DamageOutOfBounds);
}
}
