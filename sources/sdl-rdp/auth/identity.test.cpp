#include <sdl-rdp/auth/identity.hpp>

#include <sdl-rdp/abi/backend.h>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <condition_variable>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>

namespace {
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  EXPECT_EQ(Backend::IdentityText(std::u16string_view{ u"žąsis" }), "žąsis");
  EXPECT_EQ(Backend::IdentityText(std::string_view{ "Aé" }), "Aé");
}
}
