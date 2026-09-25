#include <sdl-rdp/utilities/scoped.hpp>
#include <gtest/gtest.h>
#include <sdl-rdp/utilities/contract.hpp>
#include <functional>
#include <optional>
#include <stdexcept>

namespace sdl_rdp::utilities::detail::scoped {
namespace {
using Count = std::optional<std::reference_wrapper<int>>;
auto Open(int& count) -> Count {
  ++count;
  return count;
}
auto Close(Count count) noexcept -> bool {
  --Required(count, "RAIIWrap closes only an open count").get();
  return true;
}
auto IsNull(Count const& count) noexcept -> bool {
  return !count;
}
auto MakeNull(Count& count) noexcept -> void {
  count.reset();
}
auto Enter(int& count) -> int& {
  ++count;
  return count;
}
auto Leave(int& count) noexcept -> void {
  --count;
}
using Counted = RAIIWrap<Count, Open, Close, IsNull, MakeNull>;
using Entered = RAIIWrap<int&, Enter, Leave>;
static_assert(!std::copy_constructible<Counted>);
static_assert(std::is_nothrow_move_constructible_v<Counted>);
static_assert(std::is_nothrow_move_assignable_v<Counted>);
static_assert(!std::move_constructible<Entered>);
static_assert(!std::move_constructible<RAIIWrap<Count, Open, Close, IsNull>>);
auto MoveTwiceIntoOccupied(int& first, int& second) -> void {
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
  MoveTwiceIntoOccupied(counts.front(), counts.back());
  EXPECT_EQ(counts, (std::array{ 0, 0 }));
}
TEST(ScopedResource, CloseReturnsStatusAndPreventsDoubleRelease) {
  std::array<int, 1> count{ };
  {
    Counted owner{ count.front() };
    EXPECT_TRUE(owner.Close());
    EXPECT_FALSE(owner);
  }
  EXPECT_EQ(count.front(), 0);
}
TEST(ScopedResource, ReleaseHandsTheValueOverWithoutClosing) {
  std::array<int, 1> count{ };
  {
    Counted owner{ count.front() };
    EXPECT_EQ(&Required(owner.Release(), "release hands the count over").get(), &count.front());
  }
  EXPECT_EQ(count.front(), 1);
}
TEST(ScopedResource, ReferencePairLeavesScopeDuringUnwinding) {
  int active{ };
  EXPECT_THROW(EnterAndThrow(active), std::runtime_error);
  EXPECT_EQ(active, 0);
}
}
}
