#include <sdl-rdp/utilities/text.hpp>

#include <gtest/gtest.h>
#include <array>
#include <string_view>

namespace sdl_rdp::utilities::detail::text {
static_assert(AsciiUpper('a') == 'A');
TEST(AsciiUpper, RaisesOnlyAsciiLowerCaseLetters) {
  EXPECT_EQ(AsciiUpper('z'), 'Z');
  EXPECT_EQ(AsciiUpper('Q'), 'Q');
  EXPECT_EQ(AsciiUpper('_'), '_');
  EXPECT_EQ(AsciiUpper('7'), '7');
  EXPECT_EQ(AsciiUpper('\xe9'), '\xe9');
}
TEST(CopyTerminated, CopiesTextAndClearsTheRest) {
  std::array<char, 8> field{ };
  field.fill('x');
  CopyTerminated(field, "abc");
  EXPECT_EQ(std::string_view(field.data(), field.size()), std::string_view("abc\0\0\0\0\0", 8));
}
TEST(CopyTerminated, TruncatesToLeaveTheTerminator) {
  std::array<char, 4> field{ };
  CopyTerminated(field, "abcdef");
  EXPECT_EQ(std::string_view(field.data(), field.size()), std::string_view("abc\0", 4));
}
TEST(Utf16, WidensEveryCodePoint) {
  EXPECT_EQ(Utf16("Password"), u"Password");
  EXPECT_EQ(Utf16("\xC3\xA9\xF0\x9F\x98\x80"), u"é\U0001F600");
}
TEST(Utf16, EmptyStaysEmpty) {
  EXPECT_TRUE(Utf16("").empty());
}
TEST(Utf16, RefusesInvalidUtf8) {
  EXPECT_THROW(Utf16("\xFF"), InvalidEncoding);
}
}
