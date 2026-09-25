#include <sdl-rdp/auth/identity.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <condition_variable>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>

namespace sdl_rdp::auth::detail::identity {
namespace {
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  EXPECT_EQ(IdentityText(std::u16string_view{ u"žąsis" }), "žąsis");
  EXPECT_EQ(IdentityText(std::string_view{ "Aé" }), "Aé");
}
}
}
