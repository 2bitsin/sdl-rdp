#include <sdl-rdp/utilities/narrowed.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <tuple>

TEST(Narrowed, KeepsAValueThatFits) {
  EXPECT_EQ(Backend::Narrowed<std::uint16_t>(std::size_t{ 65535 }), 65535u);
  EXPECT_EQ(Backend::Narrowed<std::uint32_t>(std::int64_t{ 7 }), 7u);
}
TEST(NarrowedDeathTest, RefusesAValueThatDoesNotFit) {
  EXPECT_DEATH(std::ignore = Backend::Narrowed<std::uint16_t>(std::size_t{ 65536 }), "fits the narrower type");
  EXPECT_DEATH(std::ignore = Backend::Narrowed<std::uint32_t>(-1), "fits the narrower type");
}
