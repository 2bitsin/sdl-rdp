#include <sdl-rdp/freerdp-facade/yuv420.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::yuv420 {
TEST(RgbToYuv420, GreyIsUniformLumaAndNeutralChroma) {
  constexpr Extent          size { .width = 16, .height = 8 };
  auto const                bgrx = std::views::repeat(std::array<std::uint8_t, 4>{ 0x80, 0x80, 0x80, 0xFF }, 16uz * 8)
                                   | std::views::join | std::ranges::to<std::vector>();
  std::vector<std::uint8_t> luma(16uz * 8);
  std::vector<std::uint8_t> blue(8uz * 4);
  std::vector<std::uint8_t> red(8uz * 4);
  ASSERT_TRUE(RgbToYuv420(bgrx, size.width * 4, size,
                          { .luma            = luma,
                            .blue_difference = blue,
                            .red_difference  = red,
                            .luma_pitch      = size.width,
                            .chroma_pitch    = size.width / 2 }));
  EXPECT_EQ(std::ranges::count(luma, luma.front()), std::ranges::ssize(luma));
  auto const neutral = [](int value) { return std::abs(value - 128) <= 1; };
  EXPECT_TRUE(std::ranges::all_of(blue, neutral));
  EXPECT_TRUE(std::ranges::all_of(red, neutral));
}
TEST(RgbToYuv420, BlueLandsInTheUPlaneAndNotTheV) {
  constexpr Extent          size  { .width = 16, .height = 8 };
  auto const                bgrx  = std::views::repeat(std::array<std::uint8_t, 4>{ 0xFF, 0x00, 0x00, 0xFF }, 16uz * 8)
                                    | std::views::join | std::ranges::to<std::vector>();
  std::vector<std::uint8_t> frame(16uz * 8 * 3 / 2);
  ASSERT_TRUE(RgbToYuv420(bgrx, size.width * 4, size, PackedI420(frame, size.width, size.height)));
  auto const planes = PackedI420(frame, size.width, size.height);
  EXPECT_TRUE(std::ranges::all_of(planes.blue_difference, [](int value) { return value > 192; }));
  EXPECT_TRUE(std::ranges::all_of(planes.red_difference, [](int value) { return value < 128; }));
}
TEST(PackedI420, LumaThenQuarterSizedUThenV) {
  std::vector<std::uint8_t> frame(8uz * 4 * 3 / 2);
  auto const                planes = PackedI420(frame, 8, 4);
  EXPECT_EQ(planes.luma.data(), frame.data());
  EXPECT_EQ(planes.luma.size(), 32u);
  EXPECT_EQ(planes.blue_difference.data(), frame.data() + 32);
  EXPECT_EQ(planes.blue_difference.size(), 8u);
  EXPECT_EQ(planes.red_difference.data(), frame.data() + 40);
  EXPECT_EQ(planes.red_difference.size(), 8u);
  EXPECT_EQ(planes.luma_pitch, 8u);
  EXPECT_EQ(planes.chroma_pitch, 4u);
}
}
