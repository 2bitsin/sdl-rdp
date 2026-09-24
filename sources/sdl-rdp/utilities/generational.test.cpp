#include <sdl-rdp/utilities/generational.hpp>

#include <gtest/gtest.h>
#include <string>

TEST(Generational, StartsEmptyAtGenerationZero) {
  Backend::Generational<std::string> const held;
  EXPECT_EQ(held.Generation(), 0u);
  EXPECT_TRUE(held.Value().empty());
}
TEST(Generational, EachReplacementAdvancesTheGeneration) {
  Backend::Generational<std::string> held;
  EXPECT_EQ(held.Replace("first"), 1u);
  EXPECT_EQ(held.Replace("second"), 2u);
  EXPECT_EQ(held.Generation(), 2u);
  EXPECT_EQ(held.Value(), "second");
}
