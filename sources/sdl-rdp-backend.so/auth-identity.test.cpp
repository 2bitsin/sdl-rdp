#include "_detail/auth-identity.hpp"

#include "_detail/handle.hpp"
#include "sdl-rdp-backend.h"

#include <condition_variable>
#include <format>
#include <gtest/gtest.h>
#include <mutex>
#include <oxbox/platform/scratch-area.hpp>
#include <thread>

namespace {
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  std::array<uint16_t, 5> unicode{ 0x017e, 0x0105, 's', 'i', 's' };
  EXPECT_EQ(Backend::IdentityText(std::span<uint16_t const>(unicode)), "žąsis");
  auto ansi = std::to_array("Aé");
  EXPECT_EQ(Backend::IdentityText(std::span<char const>(ansi).first(ansi.size() - 1)), "Aé");
}
}
