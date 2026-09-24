#include <sdl-rdp/auth/auth-identity.hpp>

#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>

namespace {
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  std::array<std::uint16_t, 5> unicode{ 0x017e, 0x0105, 's', 'i', 's' };
  EXPECT_EQ(Backend::IdentityText(std::span<std::uint16_t const>(unicode)), "žąsis");
  auto ansi = std::to_array("Aé");
  EXPECT_EQ(Backend::IdentityText(std::span<char const>(ansi).first(ansi.size() - 1)), "Aé");
}
}
