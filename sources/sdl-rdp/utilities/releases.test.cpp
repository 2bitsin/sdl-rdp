#include <sdl-rdp/utilities/releases.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace sdl_rdp::utilities::detail::releases {
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
    std::unique_ptr<std::string, Releases<Closes, Frees, Closes>> const owned{ &log };
  }
  EXPECT_EQ(log, "cfc");
}
TEST(Releases, DiscardsTheReturnedStatus) {
  std::string log;
  Releases<Frees>{ }(&log);
  EXPECT_EQ(log, "f");
}
TEST(Releases, ReleasesAnOpaqueHandleWhenItsOwnerEnds) {
  std::string log;
  {
    std::unique_ptr<void, Releases<ClosesOpaque>> const owned{ &log };
  }
  EXPECT_EQ(log, "o");
}
TEST(Releases, IsANoThrowDeleter) {
  EXPECT_TRUE(noexcept(Releases<Closes>{ }(static_cast<std::string*>(nullptr))));
}
}
