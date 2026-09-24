#include <sdl-rdp/utilities/operation-name.hpp>

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(std::is_constructible_v<Backend::OperationName, decltype("Peer")>);
static_assert(!std::is_constructible_v<Backend::OperationName, std::string>);
static_assert(!std::is_constructible_v<Backend::OperationName, std::string_view>);
static_assert(!std::is_constructible_v<Backend::OperationName, char const*>);

TEST(OperationName, ViewsTheLiteralWithoutItsTerminator) {
  Backend::OperationName const name{ "Peer logon" };
  EXPECT_EQ(name.View(), std::string_view{ "Peer logon" });
  EXPECT_EQ(name.View().size(), 10u);
}
TEST(OperationName, ConvertsFromALiteralArgument) {
  auto const view = [](Backend::OperationName name) { return name.View(); };
  EXPECT_EQ(view("Touch event"), std::string_view{ "Touch event" });
}
