#include <sdl-rdp/freerdp-facade/virtual-channel.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::freerdp_facade::detail::virtual_channel {
TEST(VirtualChannel, AChannelIsConstructedOnlyFromAnOpenedHandle) {
  EXPECT_DEATH(VirtualChannel{ ChannelHandle{ } }, "an opened channel exists");
}
}
