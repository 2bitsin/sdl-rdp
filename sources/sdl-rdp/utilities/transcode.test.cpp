#include <sdl-rdp/utilities/transcode.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::utilities::detail::transcode {
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
