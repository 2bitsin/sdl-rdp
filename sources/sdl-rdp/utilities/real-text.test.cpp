#include <sdl-rdp/utilities/real-text.hpp>

#include <gtest/gtest.h>

TEST(ParseReal, ReadsADecimal) {
  EXPECT_EQ(Backend::ParseReal<double>("363.5"), 363.5);
  EXPECT_EQ(Backend::ParseReal<double>("0"), 0.0);
}
TEST(ParseReal, RefusesTextThatIsNotWhollyANumber) {
  EXPECT_EQ(Backend::ParseReal<double>("."), std::nullopt);
  EXPECT_EQ(Backend::ParseReal<double>(""), std::nullopt);
  EXPECT_EQ(Backend::ParseReal<double>("1.5 ms"), std::nullopt);
}
