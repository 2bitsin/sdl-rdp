#include <sdl-rdp/utilities/bounded.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <tuple>

namespace sdl_rdp::utilities::detail::bounded {
namespace {
using Percent = Bounded<std::uint8_t, 1, 100>;
}
TEST(Bounded, AdmitsExactlyItsRange) {
  EXPECT_FALSE(Percent::Admits(0));
  EXPECT_TRUE(Percent::Admits(1));
  EXPECT_TRUE(Percent::Admits(100));
  EXPECT_FALSE(Percent::Admits(356));
  EXPECT_FALSE(Percent::Admits(-255));
}
TEST(Bounded, DecodesAWireValueInsideAndRefusesOneOutside) {
  EXPECT_EQ(Percent::_Decode(42).Get(), 42);
  EXPECT_EQ(Percent::_Decode(42)._Encode(), 42);
  EXPECT_THROW(std::ignore = Percent::_Decode(356), OutOfRange);
}
TEST(BoundedDeathTest, ConstructionOutsideTheRangeBreaksTheContract) {
  EXPECT_DEATH(std::ignore = Percent{ 0 }, "within its range");
}
}
