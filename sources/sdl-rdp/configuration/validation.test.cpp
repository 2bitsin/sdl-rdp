#include <sdl-rdp/configuration/validation.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::configuration::detail::validation {
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::OutOfRange;

namespace {
using sdl_rdp::utilities::support_test::OutOfRangeEnum;
auto Valid() -> sdlrdp_config {
  sdlrdp_config config{ };
  config.width  = 640;
  config.height = 480;
  config.auth   = SDLRDP_AUTH_NONE;
  config.codec  = SDLRDP_CODEC_AUTO;
  config.port   = 3389;
  return config;
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
  auto port  = Valid();
  auth.auth             = OutOfRangeEnum<sdlrdp_auth>(SDLRDP_AUTH_NLA + 1);
  size.width            = 0;
  rate.avc_bitrate_kbps = 4294968;
  codec.codec           = OutOfRangeEnum<sdlrdp_codec>(SDLRDP_CODEC_AVC420 + 1);
  port.port             = 65536;
  EXPECT_THROW(Validate(auth), InvalidChoice);
  EXPECT_THROW(Validate(size), OutOfRange);
  EXPECT_THROW(Validate(rate), OutOfRange);
  EXPECT_THROW(Validate(codec), InvalidChoice);
  EXPECT_THROW(Validate(port), OutOfRange);
}
TEST(ValidRefresh, NamesTheModeWithinItsCeiling) {
  EXPECT_EQ(ValidRefresh(0, 1), RefreshMode::Fixed);
  EXPECT_EQ(ValidRefresh(3, 10), RefreshMode::Sender);
}
TEST(ValidRefresh, RefusesUnknownModesAndCeilings) {
  EXPECT_THROW(ValidRefresh(4, 60), InvalidArguments);
  EXPECT_THROW(ValidRefresh(0, 0), InvalidArguments);
  EXPECT_THROW(ValidRefresh(1, 9), InvalidArguments);
  EXPECT_THROW(ValidRefresh(0, 2147484), InvalidArguments);
}
}
