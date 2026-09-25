#include <sdl-rdp/utilities/narrowed.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace sdl_rdp::utilities::detail::narrowed {
TEST(Narrowed, KeepsAValueThatFits) {
  EXPECT_EQ(Narrowed<std::uint16_t>(std::size_t{ 65535 }), 65535u);
  EXPECT_EQ(Narrowed<std::uint32_t>(std::int64_t{ 7 }), 7u);
  EXPECT_EQ(Narrowed<char>('A' + std::size_t{ 2 }), 'C');
}
TEST(NarrowedDeathTest, RefusesAValueThatDoesNotFit) {
  EXPECT_DEATH(std::ignore = Narrowed<std::uint16_t>(std::size_t{ 65536 }), "fits the narrower type");
  EXPECT_DEATH(std::ignore = Narrowed<std::uint32_t>(-1), "fits the narrower type");
  EXPECT_DEATH(std::ignore = Narrowed<char>(std::size_t{ 300 }), "fits the narrower type");
}
}
