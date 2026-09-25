#include <sdl-rdp/utilities/contract.hpp>

#include <gtest/gtest.h>
#include <optional>

namespace sdl_rdp::utilities::detail::contract {
TEST(Required, HandsBackTheHeldValue) {
  EXPECT_EQ(Required(std::optional{ 7 }, "a held value"), 7);
}
}
