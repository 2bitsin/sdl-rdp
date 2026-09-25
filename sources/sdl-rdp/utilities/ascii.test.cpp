#include <sdl-rdp/utilities/ascii.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::utilities::detail::ascii {
static_assert(AsciiUpper('a') == 'A');
TEST(AsciiUpper, RaisesOnlyAsciiLowerCaseLetters) {
  EXPECT_EQ(AsciiUpper('z'), 'Z');
  EXPECT_EQ(AsciiUpper('Q'), 'Q');
  EXPECT_EQ(AsciiUpper('_'), '_');
  EXPECT_EQ(AsciiUpper('7'), '7');
  EXPECT_EQ(AsciiUpper('\xe9'), '\xe9');
}
}
