#include <sdl-rdp/utilities/real-text.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::utilities::detail::real_text {
TEST(ParseReal, ReadsADecimal) {
  EXPECT_EQ(ParseReal<double>("363.5"), 363.5);
  EXPECT_EQ(ParseReal<double>("0"), 0.0);
}
TEST(ParseReal, RefusesTextThatIsNotWhollyANumber) {
  EXPECT_EQ(ParseReal<double>("."), std::nullopt);
  EXPECT_EQ(ParseReal<double>(""), std::nullopt);
  EXPECT_EQ(ParseReal<double>("1.5 ms"), std::nullopt);
}
}
