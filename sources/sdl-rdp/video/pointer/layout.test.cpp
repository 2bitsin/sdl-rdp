#include <sdl-rdp/video/pointer/layout.hpp>

#include <sdl-rdp/video/exceptions.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::video::pointer::detail::layout {
TEST(PointerLayout, CoversEveryPixel) {
  PointerLayout const layout{ { .width = 32, .height = 16 }, 31, 15 };
  EXPECT_EQ(layout.Bytes(), 32U * 16U * 4U);
  EXPECT_EQ(layout.X(), 31U);
  EXPECT_EQ(layout.Y(), 15U);
}
TEST(PointerLayout, AnEmptyShapeHidesThePointerWhateverTheHotspot) {
  EXPECT_EQ(PointerLayout({ }, 7, 9).Bytes(), 0U);
}
TEST(PointerLayout, RefusesAHotspotOutsideOrAHalfEmptyShape) {
  EXPECT_THROW(PointerLayout({ .width = 32, .height = 16 }, 32, 0), InvalidPointerLayout);
  EXPECT_THROW(PointerLayout({ .width = 32, .height = 16 }, 0, 16), InvalidPointerLayout);
  EXPECT_THROW(PointerLayout({ .width = 32, .height = 0 }, 0, 0), InvalidPointerLayout);
}
TEST(PointerLayout, RefusesMoreThanALargePointer) {
  auto const over = LargePointerLimit + 1;
  EXPECT_THROW(PointerLayout({ .width = over, .height = 1 }, 0, 0), InvalidPointerLayout);
  EXPECT_THROW(PointerLayout({ .width = 1, .height = over }, 0, 0), InvalidPointerLayout);
}
}
