#include <sdl-rdp/video/gfx/protocol.hpp>

#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/regions.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>

namespace {
TEST(GraphicsCapability, HighestSupportedVersion) {
  std::array<RDPGFX_CAPSET, 4> caps{ { { .version = RDPGFX_CAPVERSION_81 , .length = 4 , .flags = 0 },
                                       { .version = 0xffffffff           , .length = 16, .flags = 0 },
                                       { .version = RDPGFX_CAPVERSION_107, .length = 4 , .flags = 0 },
                                       { .version = RDPGFX_CAPVERSION_10 , .length = 4 , .flags = 0 } } };
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  std::ranges::reverse(caps);
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  EXPECT_EQ(Backend::SelectCapability({ }).version, 0u);
  RDPGFX_CAPSET unknown{ 0xffffffff, 16, 0 };
  EXPECT_EQ(Backend::SelectCapability({ &unknown, 1 }).version, 0u);
}
TEST(GraphicsCapability, Version101ReservedLength) {
  std::array<RDPGFX_CAPSET, 2> caps{ { { .version = RDPGFX_CAPVERSION_10 , .length = 4, .flags = 0          },
                                       { .version = RDPGFX_CAPVERSION_101, .length = 4, .flags = 0xffffffff } } };
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
  for (std::uint32_t const version : { RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
                                       RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107 }) {
    RDPGFX_CAPSET cap      { version, 4, 0xffffffff };
    auto          selected = Backend::SelectCapability({ &cap, 1 });
    EXPECT_EQ(selected.flags, handled | (version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0));
    EXPECT_EQ(selected.length, 4u);
    cap.length = 3;
    EXPECT_EQ(Backend::SelectCapability({ &cap, 1 }).version, 0u);
  }
}
TEST(GraphicsTimestamp, PacksIndependentFields) {
  SYSTEMTIME time{ };
  EXPECT_EQ(Backend::FrameTimestamp(time), 0u);
  time.wHour = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00400000u);
  time.wHour   = 0;
  time.wMinute = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00010000u);
  time.wMinute = 0;
  time.wSecond = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00000400u);
  time.wSecond       = 0;
  time.wMilliseconds = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 1u);
  time.wHour         = 23;
  time.wMinute       = 59;
  time.wSecond       = 59;
  time.wMilliseconds = 999;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x05fbefe7u);
}
TEST(Avc, Bitrate) {
  EXPECT_EQ(Backend::Avc::Bitrate({ 1920, 1080 }), 16000000u);
  EXPECT_EQ(Backend::Avc::Bitrate({ 960, 540 }), 4000000u);
  EXPECT_EQ(Backend::Avc::Bitrate({ 320, 200 }), 2000000u);
  EXPECT_EQ(Backend::Avc::Bitrate({ 320, 200 }, 1234), 1234000u);
  EXPECT_EQ(Backend::Avc::Bitrate({ 32766, 32766 }), UINT32_MAX);
}
TEST(Avc, ReplicatesPadding) {
  std::array<std::uint8_t, 32> source{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 99, 99, 99, 99,
                                       13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 99, 99, 99, 99 };
  std::vector<std::uint8_t>    padded(16uz * 16 * 4);
  std::copy_n(source.data(), 12, padded.data());
  std::copy_n(source.data() + 16, 12, padded.data() + 64);
  Backend::Avc::ReplicateEdges(padded, { .width = 3, .height = 2 });
  ASSERT_EQ(padded.size(), 16u * 16 * 4);
  auto const axis    = std::views::iota(0uz, 16uz);
  auto const channel = std::views::iota(0uz, 4uz);
  for (auto [y, x, c] : std::views::cartesian_product(axis, axis, channel))
    EXPECT_EQ(padded[(((y * 16) + x) * 4) + c], source[(std::min(y, 1uz) * 16) + (std::min(x, 2uz) * 4) + c]);
}
auto ThenRegionBounds(Backend::Avc::Regions const& regions) -> void {
  EXPECT_EQ(regions.Bounds().x, 2);
  EXPECT_EQ(regions.Bounds().y, 3);
  EXPECT_EQ(regions.Bounds().w, 22);
  EXPECT_EQ(regions.Bounds().h, 21);
}
auto ThenRegionQuality(auto const& q) -> void {
  EXPECT_EQ(q.qpVal, 0x9a);
  EXPECT_EQ(q.qualityVal, 100);
  EXPECT_EQ(q.p, 1);
  EXPECT_EQ(q.qp, 26);
}
TEST(Avc, RegionMetablock) {
  Backend::Avc::Regions regions;
  regions.Add({ 17, 19, 7, 5 });
  regions.Add({ 2, 3, 4, 6 });
  EXPECT_EQ(regions.Bytes(), 24u);
  ThenRegionBounds(regions);
  EXPECT_EQ(regions.Areas()[0].right, 24);
  EXPECT_EQ(regions.Areas()[0].bottom, 24);
  for (auto q : regions.Quality()) {
    ThenRegionQuality(q);
  }
}
auto ThenAvailableCapability(RDPGFX_CAPSET const& cap, std::uint32_t version) -> void {
  for (bool const available : { false, true }) {
    auto          selected = Backend::SelectCapability({ &cap, 1 }, available);
    std::uint32_t expected = 0;
    if (version == RDPGFX_CAPVERSION_81 && available) expected = RDPGFX_CAPS_FLAG_AVC420_ENABLED;
    if (version >= RDPGFX_CAPVERSION_10 && version != RDPGFX_CAPVERSION_101 && !available)
      expected = RDPGFX_CAPS_FLAG_AVC_DISABLED;
    EXPECT_EQ(selected.flags, expected) << version << ' ' << available;
  }
}
TEST(GraphicsCapability, AllowsAvcWhenOfferedAndAvailable) {
  for (std::uint32_t const version : { RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
                                       RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107 }) {
    RDPGFX_CAPSET cap{ version, version == RDPGFX_CAPVERSION_101 ? 16u : 4u, RDPGFX_CAPS_FLAG_AVC420_ENABLED };
    ThenAvailableCapability(cap, version);
    cap.flags = version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0;
    auto selected = Backend::SelectCapability({ &cap, 1 }, true);
    EXPECT_EQ(selected.flags,
              version >= RDPGFX_CAPVERSION_10 && version != RDPGFX_CAPVERSION_101 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0u);
  }
}
TEST(GraphicsCapability, WithoutEncoderTheConfirmedSetNeverAllowsAvc) {
  for (std::uint32_t const version : { RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10, RDPGFX_CAPVERSION_107 }) {
    RDPGFX_CAPSET const offered{ version, 4u, RDPGFX_CAPS_FLAG_AVC420_ENABLED };
    EXPECT_TRUE(Backend::AllowsAvc(offered)) << version;
    EXPECT_FALSE(Backend::AllowsAvc(Backend::SelectCapability({ &offered, 1 }, false))) << version;
  }
}
}
