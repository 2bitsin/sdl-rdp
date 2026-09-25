#include <sdl-rdp/utilities/releases.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {
auto Closes(std::string* log) -> void {
  log->push_back('c');
}
auto Frees(std::string* log) -> int {
  log->push_back('f');
  return -1;
}
auto ClosesOpaque(void* log) -> void {
  static_cast<std::string*>(log)->push_back('o');
}
}
TEST(Releases, CallsEachReleaseInTheOrderGiven) {
  std::string log;
  {
    std::unique_ptr<std::string, Backend::Releases<Closes, Frees, Closes>> const owned{ &log };
  }
  EXPECT_EQ(log, "cfc");
}
TEST(Releases, DiscardsTheReturnedStatus) {
  std::string log;
  Backend::Releases<Frees>{ }(&log);
  EXPECT_EQ(log, "f");
}
TEST(Releases, ReleasesAnOpaqueHandleWhenItsOwnerEnds) {
  std::string log;
  {
    std::unique_ptr<void, Backend::Releases<ClosesOpaque>> const owned{ &log };
  }
  EXPECT_EQ(log, "o");
}
TEST(Releases, IsANoThrowDeleter) {
  EXPECT_TRUE(noexcept(Backend::Releases<Closes>{ }(static_cast<std::string*>(nullptr))));
}
