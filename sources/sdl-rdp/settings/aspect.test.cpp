#include <sdl-rdp/settings/aspect.hpp>

#include <sdl-rdp/settings/exceptions.hpp>

#include <gtest/gtest.h>
#include <string_view>
#include <tuple>

namespace sdl_rdp::settings::detail::aspect {
TEST(Aspect, ReadsTwoPositiveWholeNumbersOrNothing) {
  EXPECT_EQ(Aspect::Parsed(" 16:9 "), (Aspect{ 16, 9 }));
  EXPECT_EQ(Aspect::Parsed(" "), Aspect::None());
  for (std::string_view const text : { "1:0", "4:3:2", "4:x", "4 : 3", "-4:3" })
    EXPECT_EQ(Aspect::Parsed(text), std::nullopt) << text;
}
TEST(Aspect, TextRoundTripsAndNoneIsEmpty) {
  EXPECT_EQ(Aspect::_Decode(Aspect{ 4, 3 }._Encode()), (Aspect{ 4, 3 }));
  EXPECT_EQ(Aspect::None().Text(), "");
  EXPECT_TRUE(Aspect::None().IsNone());
  EXPECT_EQ(Aspect{ }, Aspect::None());
  EXPECT_EQ((Aspect{ 4, 3 }.Ratio()), (AspectRatio{ .numerator = 4, .denominator = 3 }));
  EXPECT_THROW(std::ignore = Aspect::_Decode("4:0"), InvalidSettingValue);
}
TEST(Aspect, NoneHasNoRatio) {
  EXPECT_FALSE(Aspect::None().Ratio().has_value());
}
TEST(AspectDeathTest, AStatedRatioIsPositive) {
  EXPECT_DEATH(std::ignore = (Aspect{ 4, 0 }), "denominator is positive");
}
}
