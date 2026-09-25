#include <sdl-rdp/configuration/validation.hpp>

#include <sdl-rdp/configuration/auth-mode.hpp>
#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>

#include <gtest/gtest.h>
#include <utility>

namespace sdl_rdp::configuration::detail::validation {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::Setup;
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::OutOfRange;

namespace {
using sdl_rdp::utilities::support_test::OutOfRangeEnum;
auto Valid() -> Setup {
  return { .port = 3389, .width = 640, .height = 480, .codec = Codec::Auto, .auth = AuthMode::None };
}
}
TEST(Validate, AcceptsAPlainConfiguration) {
  EXPECT_NO_THROW(Validate(Valid()));
}
TEST(Validate, RefusesEachFieldOutsideItsRange) {
  auto auth  = Valid();
  auto size  = Valid();
  auto rate  = Valid();
  auto codec = Valid();
  auth.auth             = OutOfRangeEnum<AuthMode>(std::to_underlying(AuthMode::Nla) + 1);
  size.width            = 0;
  rate.avc_bitrate_kbps = 4294968;
  codec.codec           = OutOfRangeEnum<Codec>(std::to_underlying(Codec::Avc420) + 1);
  EXPECT_THROW(Validate(auth), InvalidChoice);
  EXPECT_THROW(Validate(size), OutOfRange);
  EXPECT_THROW(Validate(rate), OutOfRange);
  EXPECT_THROW(Validate(codec), InvalidChoice);
}
TEST(ValidateRefresh, AcceptsACeilingTheModeCanPace) {
  EXPECT_NO_THROW(ValidateRefresh(RefreshMode::Fixed, 1));
  EXPECT_NO_THROW(ValidateRefresh(RefreshMode::Sender, 10));
}
TEST(ValidateRefresh, RefusesACeilingOutsideTheModesRange) {
  EXPECT_THROW(ValidateRefresh(RefreshMode::Fixed, 0), InvalidArguments);
  EXPECT_THROW(ValidateRefresh(RefreshMode::Client, 9), InvalidArguments);
  EXPECT_THROW(ValidateRefresh(RefreshMode::Fixed, 2147484), InvalidArguments);
}
}
