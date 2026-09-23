#include "_detail/gfx-protocol.hpp"
#include <gtest/gtest.h>
#include <array>

namespace {
TEST(GraphicsCapability, HighestSupportedVersion) {
  std::array<RDPGFX_CAPSET, 4> caps{{{RDPGFX_CAPVERSION_81, 4, 0},
    {0xffffffff, 16, 0}, {RDPGFX_CAPVERSION_107, 4, 0}, {RDPGFX_CAPVERSION_10, 4, 0}}};
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  std::reverse(caps.begin(), caps.end());
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  EXPECT_EQ(Backend::SelectCapability({}).version, 0u);
  RDPGFX_CAPSET unknown{0xffffffff, 16, 0};
  EXPECT_EQ(Backend::SelectCapability({&unknown, 1}).version, 0u);
}
TEST(GraphicsCapability, Version101ReservedLength) {
  std::array<RDPGFX_CAPSET, 2> caps{{{RDPGFX_CAPVERSION_10, 4, 0}, {RDPGFX_CAPVERSION_101, 4, 0xffffffff}}};
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_10);
  caps.back().length = 16;
  auto selected = Backend::SelectCapability(caps);
  EXPECT_EQ(selected.version, RDPGFX_CAPVERSION_101);
  EXPECT_EQ(selected.length, 16u);
  EXPECT_EQ(selected.flags, 0u);
}
TEST(GraphicsCapability, MasksFlagsAndDisablesAvc) {
  constexpr auto handled = RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE
    | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE;
  for (auto version : {RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
      RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107}) {
    RDPGFX_CAPSET cap{version, 4, 0xffffffff};
    auto selected = Backend::SelectCapability({&cap, 1});
    EXPECT_EQ(selected.flags, handled | (version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0));
    EXPECT_EQ(selected.length, 4u);
    cap.length = 3;
    EXPECT_EQ(Backend::SelectCapability({&cap, 1}).version, 0u);
  }
}
TEST(GraphicsTimestamp, PacksIndependentFields) {
  SYSTEMTIME time{};
  EXPECT_EQ(Backend::FrameTimestamp(time), 0u);
  time.wHour = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00400000u);
  time.wHour = 0; time.wMinute = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00010000u);
  time.wMinute = 0; time.wSecond = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00000400u);
  time.wSecond = 0; time.wMilliseconds = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 1u);
  time.wHour = 23; time.wMinute = 59; time.wSecond = 59; time.wMilliseconds = 999;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x05fbefe7u);
}
}
