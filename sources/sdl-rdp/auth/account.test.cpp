#include <sdl-rdp/auth/account.hpp>

#include <sdl-rdp/utilities/wiped-string.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::auth::detail::account {
using sdl_rdp::configuration::Setup;
namespace {
// MS-NLMP 4.2.2.1.2: NTOWFv1 of "Password".
constexpr std::array<std::uint8_t, 16> PasswordHash{ 0xa4, 0xf4, 0x9c, 0x40, 0x65, 0x10, 0xbd, 0xca,
                                                     0xb6, 0x82, 0x4e, 0xe7, 0xc3, 0x0f, 0xd8, 0x52 };
auto Pair() -> Setup {
  return { .user = "User", .password = WipedString{ "Password" }, .domain = "Domain" };
}
auto AnyDomain() -> Setup {
  auto config = Pair();
  config.domain.reset();
  return config;
}
}
TEST(Account, VerifiesOnlyTheConfiguredPair) {
  auto const    config  = Pair();
  Account const account { config };
  EXPECT_TRUE(account.Verifies("Domain", "User", "Password"));
  EXPECT_FALSE(account.Verifies("Domain", "User", "password"));
  EXPECT_FALSE(account.Verifies("Domain", "User", "Password1"));
  EXPECT_FALSE(account.Verifies("Domain", "User", ""));
  EXPECT_FALSE(account.Verifies("Domain", "user", "Password"));
  EXPECT_FALSE(account.Verifies("Other", "User", "Password"));
}
TEST(Account, AnyDomainWhenNoneIsConfigured) {
  auto const    config  = AnyDomain();
  Account const account { config };
  EXPECT_TRUE(account.Verifies("", "User", "Password"));
  EXPECT_TRUE(account.Verifies("Other", "User", "Password"));
}
TEST(Account, NoPairWithoutUserOrPassword) {
  auto without_user     = AnyDomain();
  auto without_password = AnyDomain();
  without_user.user.reset();
  without_password.password.reset();
  EXPECT_FALSE(Account{ without_user }.Verifies("", "", "Password"));
  EXPECT_FALSE(Account{ without_password }.Verifies("", "User", ""));
  EXPECT_FALSE(Account{ without_user }.NtHash("", "").has_value());
  EXPECT_FALSE(Account{ without_password }.NtHash("", "User").has_value());
}
TEST(Account, NtHashIsTheNtOwfV1OfThePassword) {
  auto const config  = Pair();
  auto const matches = [](NtOwf const& hash) { return std::ranges::equal(hash.Bytes(), PasswordHash); };
  EXPECT_TRUE(Account{ config }.NtHash("Domain", "User").transform(matches).value_or(false));
}
TEST(Account, NoNtHashForAnotherName) {
  auto const config = Pair();
  EXPECT_FALSE(Account{ config }.NtHash("Domain", "Someone").has_value());
  EXPECT_FALSE(Account{ config }.NtHash("Other", "User").has_value());
}
}
