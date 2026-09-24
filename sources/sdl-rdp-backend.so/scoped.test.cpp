#include "_detail/scoped.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

namespace {
auto Open(int* count) -> int* {
  ++*count;
  return count;
}
auto Close(int* count) noexcept -> bool {
  --*count;
  return true;
}
auto IsNull(int const* count) noexcept -> bool { return count == nullptr; }
auto MakeNull(int*& count) noexcept    -> void { count = nullptr; }
auto Enter(int& count)                 -> int& {
  ++count;
  return count;
}
auto Leave(int& count) noexcept -> void { --count; }
using Counted = utilities::RAIIWrap<int*, Open, Close, IsNull, MakeNull>;
using Entered = utilities::RAIIWrap<int&, Enter, Leave>;
static_assert(!std::copy_constructible<Counted>);
static_assert(std::is_nothrow_move_constructible_v<Counted>);
static_assert(std::is_nothrow_move_assignable_v<Counted>);
static_assert(!std::move_constructible<Entered>);
static_assert(!std::move_constructible<utilities::RAIIWrap<int*, Open, Close, IsNull>>);
auto MoveTwiceIntoOccupied(int* first, int* second) -> void {
  Counted source     { first             };
  Counted destination{ second            };
  Counted moved      { std::move(source) };
  destination = std::move(moved);
}
auto EnterAndThrow(int& active) -> void {
  Entered const scope{ active };
  throw std::runtime_error("unwind");
}
TEST(ScopedResource, MovedFromOwnersReleaseNothingAndAssignmentClosesPrevious) {
  std::array<int, 2> counts{ };
  MoveTwiceIntoOccupied(&counts.front(), &counts.back());
  EXPECT_EQ(counts, (std::array{0, 0}));
}
TEST(ScopedResource, CloseReturnsStatusAndPreventsDoubleRelease) {
  std::array<int, 1> count{ };
  {
    Counted owner{ count.data() };
    EXPECT_TRUE(owner.Close());
    EXPECT_FALSE(owner);
  }
  EXPECT_EQ(count.front(), 0);
}
TEST(ScopedResource, ReleaseHandsTheValueOverWithoutClosing) {
  std::array<int, 1> count{ };
  {
    Counted owner{ count.data() };
    EXPECT_EQ(owner.Release(), count.data());
  }
  EXPECT_EQ(count.front(), 1);
}
TEST(ScopedResource, ReferencePairLeavesScopeDuringUnwinding) {
  int active{ };
  EXPECT_THROW(EnterAndThrow(active), std::runtime_error);
  EXPECT_EQ(active, 0);
}
}
