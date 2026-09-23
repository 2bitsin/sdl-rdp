#include "_detail/auth-identity.hpp"

#include "_detail/headless-client.hpp"
#include "_detail/state.hpp"
#include "sdl-rdp-backend.h"

#include <condition_variable>
#include <format>
#include <gtest/gtest.h>
#include <mutex>
#include <oxbox/platform/scratch-area.hpp>
#include <thread>

namespace {
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  std::array<UINT16, 5> unicode{ 0x017e, 0x0105, 's', 'i', 's' };
  EXPECT_EQ(Backend::IdentityText(unicode.data(), 5, SEC_WINNT_AUTH_IDENTITY_UNICODE), "žąsis");
  auto ansi = std::to_array("Aé");
  EXPECT_EQ(
      Backend::IdentityText(reinterpret_cast<UINT16*>(ansi.data()), ansi.size() - 1, SEC_WINNT_AUTH_IDENTITY_ANSI),
      "Aé");
}
}
