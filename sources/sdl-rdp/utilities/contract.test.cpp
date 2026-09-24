#include <sdl-rdp/utilities/contract.hpp>

#include <gtest/gtest.h>
#include <optional>

TEST(Required, HandsBackTheHeldValue) {
  EXPECT_EQ(Backend::Required(std::optional{ 7 }, "a held value"), 7);
}
