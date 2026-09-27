#include <sdl-rdp/video/gfx/protocol.hpp>

#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/regions.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace sdl_rdp::video::gfx::detail::protocol {
using sdl_rdp::utilities::support_test::OutOfRangeEnum;
using sdl_rdp::video::avc::Bitrate;
using sdl_rdp::video::avc::Regions;
using sdl_rdp::video::avc::ReplicateEdges;
namespace {
constexpr auto NoFlags  = GfxCapsFlags{ };
const auto     AllFlags = OutOfRangeEnum<GfxCapsFlags>(0xffffffff);
const auto     Unknown  = OutOfRangeEnum<GfxVersion>(0xffffffff);
auto Version(std::optional<GfxCapability> cap) -> std::optional<GfxVersion> {
  return cap.transform(&GfxCapability::version);
}
auto Flags(std::optional<GfxCapability> cap) -> std::optional<GfxCapsFlags> {
  return cap.transform(&GfxCapability::flags);
}
TEST(GraphicsCapability, HighestSupportedVersion) {
  std::array<GfxCapability, 4> caps{ { { .version = GfxVersion::V81 , .flags = NoFlags },
                                       { .version = Unknown         , .flags = NoFlags },
                                       { .version = GfxVersion::V107, .flags = NoFlags },
                                       { .version = GfxVersion::V10 , .flags = NoFlags } } };
  EXPECT_EQ(Version(SelectCapability(caps)), GfxVersion::V107);
  std::ranges::reverse(caps);
  EXPECT_EQ(Version(SelectCapability(caps)), GfxVersion::V107);
  EXPECT_FALSE(SelectCapability({ }).has_value());
  GfxCapability const unknown{ .version = Unknown, .flags = NoFlags };
  EXPECT_FALSE(SelectCapability(std::array{ unknown }).has_value());
}
TEST(GraphicsCapability, Version101AnswersWithoutFlags) {
  std::array<GfxCapability, 2> caps{ { { .version = GfxVersion::V10 , .flags = NoFlags  },
                                       { .version = GfxVersion::V101, .flags = AllFlags } } };
  EXPECT_EQ(Version(SelectCapability(caps)), GfxVersion::V101);
  EXPECT_EQ(Flags(SelectCapability(caps)), NoFlags);
}
TEST(GraphicsCapability, MasksFlagsAndDisablesAvc) {
  constexpr auto handled = GfxCapsFlags::ThinClient | GfxCapsFlags::SmallCache | GfxCapsFlags::ScaledMapDisable;
  for (auto const version : { GfxVersion::V8, GfxVersion::V81, GfxVersion::V10, GfxVersion::V102, GfxVersion::V107 }) {
    GfxCapability const cap      { .version = version, .flags = AllFlags };
    auto const          selected = SelectCapability(std::array{ cap });
    EXPECT_EQ(Flags(selected), version >= GfxVersion::V10 ? handled | GfxCapsFlags::AvcDisabled : handled);
  }
}
TEST(Avc, Bitrate) {
  EXPECT_EQ(Bitrate({ 1920, 1080 }), 16000000u);
  EXPECT_EQ(Bitrate({ 960, 540 }), 4000000u);
  EXPECT_EQ(Bitrate({ 320, 200 }), 2000000u);
  EXPECT_EQ(Bitrate({ 320, 200 }, 1234), 1234000u);
  EXPECT_EQ(Bitrate({ 32766, 32766 }), UINT32_MAX);
}
TEST(Avc, ReplicatesPadding) {
  std::array<std::uint8_t, 32> source{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 99, 99, 99, 99,
                                       13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 99, 99, 99, 99 };
  std::vector<std::uint8_t>    padded(16uz * 16 * 4);
  std::copy_n(source.data(), 12, padded.data());
  std::copy_n(source.data() + 16, 12, padded.data() + 64);
  ReplicateEdges(padded, { .width = 3, .height = 2 });
  ASSERT_EQ(padded.size(), 16u * 16 * 4);
  auto const axis    = std::views::iota(0uz, 16uz);
  auto const channel = std::views::iota(0uz, 4uz);
  for (auto [y, x, c] : std::views::cartesian_product(axis, axis, channel))
    EXPECT_EQ(padded[(((y * 16) + x) * 4) + c], source[(std::min(y, 1uz) * 16) + (std::min(x, 2uz) * 4) + c]);
}
auto ThenRegionBounds(Regions const& regions) -> void {
  EXPECT_EQ(regions.Bounds().x, 2);
  EXPECT_EQ(regions.Bounds().y, 3);
  EXPECT_EQ(regions.Bounds().w, 22);
  EXPECT_EQ(regions.Bounds().h, 21);
}
TEST(Avc, RegionMetablock) {
  Regions regions;
  regions.Add({ .x = 17, .y = 19, .w = 7, .h = 5 });
  regions.Add({ .x = 2, .y = 3, .w = 4, .h = 6 });
  EXPECT_EQ(regions.Bytes(), 24u);
  ThenRegionBounds(regions);
  auto const metablock = regions.Metablock();
  ASSERT_EQ(metablock.regions.size(), 2u);
  EXPECT_EQ(metablock.regions.front().x + metablock.regions.front().w, 24);
  EXPECT_EQ(metablock.regions.front().y + metablock.regions.front().h, 24);
  EXPECT_EQ(metablock.quality.qp, 26);
  EXPECT_EQ(metablock.quality.quality, 100);
  EXPECT_TRUE(metablock.quality.progressive);
}
auto ThenAvailableCapability(GfxCapability const& cap) -> void {
  for (bool const available : { false, true }) {
    auto const version  = cap.version;
    auto const selected = SelectCapability(std::array{ cap }, available);
    auto       expected = NoFlags;
    if (version == GfxVersion::V81 && available) expected = GfxCapsFlags::Avc420Enabled;
    if (version >= GfxVersion::V10 && version != GfxVersion::V101 && !available) expected = GfxCapsFlags::AvcDisabled;
    EXPECT_EQ(Flags(selected), expected) << std::to_underlying(version) << ' ' << available;
  }
}
TEST(GraphicsCapability, AllowsAvcWhenOfferedAndAvailable) {
  for (auto const version :
       { GfxVersion::V8, GfxVersion::V81, GfxVersion::V10, GfxVersion::V101, GfxVersion::V102, GfxVersion::V107 }) {
    GfxCapability cap{ .version = version, .flags = GfxCapsFlags::Avc420Enabled };
    ThenAvailableCapability(cap);
    cap.flags = version >= GfxVersion::V10 ? GfxCapsFlags::AvcDisabled : NoFlags;
    auto const selected = SelectCapability(std::array{ cap }, true);
    EXPECT_EQ(Flags(selected),
              version >= GfxVersion::V10 && version != GfxVersion::V101 ? GfxCapsFlags::AvcDisabled : NoFlags);
  }
}
TEST(GraphicsCapability, WithoutEncoderTheConfirmedSetNeverAllowsAvc) {
  for (auto const version : { GfxVersion::V81, GfxVersion::V10, GfxVersion::V107 }) {
    GfxCapability const offered{ .version = version, .flags = GfxCapsFlags::Avc420Enabled };
    EXPECT_TRUE(AllowsAvc(offered)) << std::to_underlying(version);
    EXPECT_EQ(SelectCapability(std::array{ offered }, false).transform(AllowsAvc), std::optional{ false })
        << std::to_underlying(version);
  }
}
}
}
