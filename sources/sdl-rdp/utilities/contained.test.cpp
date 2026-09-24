#include <sdl-rdp/utilities/contained.hpp>

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
auto Collecting(std::vector<std::string>& texts) {
  return [&texts](std::string_view text) { texts.emplace_back(text); };
}
}
TEST(Contained, PassesTheBodysResultWithoutReporting) {
  std::vector<std::string> texts;
  EXPECT_EQ(Backend::Contained(-1, [] { return 7; }, Collecting(texts)), 7);
  EXPECT_TRUE(texts.empty());
}
TEST(Contained, TurnsAStandardExceptionIntoTheFailureAndReportsItsText) {
  std::vector<std::string> texts;
  EXPECT_EQ(
      Backend::Contained(-1, []() -> int { throw std::runtime_error("callback failed"); }, Collecting(texts)), -1);
  EXPECT_EQ(texts, std::vector<std::string>{ "callback failed" });
}
TEST(Contained, TurnsANonStandardExceptionIntoTheFailureAndReportsItUnknown) {
  std::vector<std::string> texts;
  EXPECT_EQ(Backend::Contained(-2, []() -> int { throw 42; }, Collecting(texts)), -2);
  EXPECT_EQ(texts, std::vector<std::string>{ std::string{ Backend::UnknownException } });
}
TEST(Contained, KeepsTheFailureWhenTheSinkThrows) {
  auto const throwing = [](std::string_view) { throw std::runtime_error("sink failed"); };
  EXPECT_EQ(Backend::Contained(-1, []() -> int { throw std::runtime_error("callback failed"); }, throwing), -1);
}
TEST(Contained, IsNoexcept) {
  EXPECT_TRUE(noexcept(Backend::Contained(0, [] { return 0; }, [](std::string_view) { })));
}
