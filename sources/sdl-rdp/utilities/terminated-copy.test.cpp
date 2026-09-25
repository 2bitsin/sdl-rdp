#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <gtest/gtest.h>
#include <array>
#include <string_view>

namespace sdl_rdp::utilities::detail::terminated_copy {
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
}
