#include <sdl-rdp/picture/frame-layout.hpp>

#include <sdl-rdp/picture/exceptions.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::picture::detail::frame_layout {
using sdl_rdp::utilities::OutOfRange;

TEST(FrameLayout, CoversEveryRowToTheLastPixel) {
  FrameLayout const layout{ 320, 200, 1536 };
  EXPECT_EQ(layout.Size().width, 320U);
  EXPECT_EQ(layout.Size().height, 200U);
  EXPECT_EQ(layout.Pitch(), 1536U);
  EXPECT_EQ(layout.Bytes(), (1536U * 199U) + (320U * 4U));
}
TEST(FrameLayout, RefusesAPitchShorterThanARow) {
  EXPECT_THROW(FrameLayout(320, 200, 1279), ShortPitch);
  EXPECT_THROW(FrameLayout(320, 200, -1), ShortPitch);
}
TEST(FrameLayout, RefusesSizesOutsideRdp) {
  EXPECT_THROW(FrameLayout(0, 200, 1280), OutOfRange);
  EXPECT_THROW(FrameLayout(320, 0, 1280), OutOfRange);
}
}
