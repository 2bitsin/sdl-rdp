#include <sdl-rdp/freerdp-facade/ntlm.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::freerdp_facade::detail::ntlm {
namespace {
// MS-NLMP 4.2.2.1.2 and 4.2.4.1.1: User "User", UserDom "Domain", Passwd "Password".
constexpr std::array<std::uint8_t, 16> PasswordV1{ 0xa4, 0xf4, 0x9c, 0x40, 0x65, 0x10, 0xbd, 0xca,
                                                   0xb6, 0x82, 0x4e, 0xe7, 0xc3, 0x0f, 0xd8, 0x52 };
constexpr std::array<std::uint8_t, 16> PasswordV2{ 0x0c, 0x86, 0x8a, 0x40, 0x3b, 0xfd, 0x7a, 0x93,
                                                   0xa3, 0x00, 0x1e, 0xf2, 0x2e, 0xf0, 0x2e, 0x3f };
auto Equal(NtOwf const& hash, std::array<std::uint8_t, 16> const& expected) -> bool {
  return std::ranges::equal(hash.Bytes(), expected);
}
}
TEST(NtOwf, StartsZeroed) {
  EXPECT_TRUE(Equal(NtOwf{ }, { }));
}
TEST(NtOwf, V1MatchesTheSpecificationVector) {
  EXPECT_TRUE(Equal(NtOwfV1(u"Password"), PasswordV1));
}
TEST(NtOwf, V2FromTheV1HashMatchesTheSpecificationVector) {
  EXPECT_TRUE(Equal(NtOwfV2(NtOwfV1(u"Password"), u"User", u"Domain"), PasswordV2));
}
TEST(NtOwf, CopiesCarryTheSameBytes) {
  auto const original = NtOwfV1(u"Password");
  NtOwf      copy     = original;
  EXPECT_TRUE(Equal(copy, PasswordV1));
  copy.Bytes()[0] = 0;
  EXPECT_TRUE(Equal(original, PasswordV1));
}
}
