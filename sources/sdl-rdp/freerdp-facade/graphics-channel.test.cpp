#include <sdl-rdp/freerdp-facade/graphics-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/server/rdpgfx.h>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <gmock/gmock.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::graphics_channel {
class GraphicsChannelProbe {
public:
  static auto Context(GraphicsChannel const& channel) -> RdpgfxServerContext& {
    return channel.Context();
  }
};
namespace {
using namespace std::chrono_literals;
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using testing::ElementsAre;
using testing::FieldsAre;

struct Recorded {
  bool                         accepts   { true };
  bool                         throws    { };
  std::vector<GfxCapability>   advertised;
  std::vector<FrameAck>        acks;
  std::vector<QoeAck>          qoes;
  std::optional<std::uint32_t> assigned;
  std::vector<std::string>     failures;
};
class Recorder final : public RecordedFailures<GraphicsChannelEvents> {
public:
  explicit Recorder(Recorded& recorded) : RecordedFailures{ recorded.failures }, _recorded{ recorded } { }
  auto     CapsAdvertise(std::span<GfxCapability const> advertised) -> bool override {
    if (_recorded.throws) throw std::runtime_error{ "handler failed" };
    _recorded.advertised.assign(advertised.begin(), advertised.end());
    return _recorded.accepts;
  }
  auto FrameAcknowledge(FrameAck ack) -> void override {
    if (_recorded.throws) throw std::runtime_error{ "handler failed" };
    _recorded.acks.push_back(ack);
  }
  auto QoeFrameAcknowledge(QoeAck ack) -> void override {
    _recorded.qoes.push_back(ack);
  }
  auto ChannelAssigned(std::uint32_t id) -> void override {
    _recorded.assigned = id;
  }

private:
  Recorded& _recorded;
};
struct Wire {
  std::optional<RDPGFX_CAPSET>           confirmed;
  std::vector<MONITOR_DEF>               monitors;
  bool                                   avc420   { };
  std::vector<RECTANGLE_16>              regions;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
};
auto Written() -> Wire& {
  static Wire written;
  return written;
}
// abi: psRdpgfxCapsConfirm
auto Confirmed(RdpgfxServerContext* /*context*/, RDPGFX_CAPS_CONFIRM_PDU const* confirm) -> std::uint32_t {
  Expects(confirm != nullptr, "the confirm is supplied");
  Written().confirmed = *confirm->capsSet;
  return CHANNEL_RC_OK;
}
// abi: psRdpgfxResetGraphics
auto Reset(RdpgfxServerContext* /*context*/, RDPGFX_RESET_GRAPHICS_PDU const* reset) -> std::uint32_t {
  Expects(reset != nullptr, "the reset is supplied");
  Written().monitors.assign_range(std::span(reset->monitorDefArray, reset->monitorCount));
  return CHANNEL_RC_OK;
}
// abi: psRdpgfxSurfaceCommand
auto Commanded(RdpgfxServerContext* /*context*/, RDPGFX_SURFACE_COMMAND const* command) -> std::uint32_t {
  Expects(command != nullptr, "the command is supplied");
  auto& written = Written();
  written.avc420 = command->extra != nullptr;
  if (!written.avc420) return CHANNEL_RC_OK;
  auto const& meta = static_cast<RDPGFX_AVC420_BITMAP_STREAM const*>(command->extra)->meta;
  written.regions.assign_range(std::span(meta.regionRects, meta.numRegionRects));
  written.quality.assign_range(std::span(meta.quantQualityVals, meta.numRegionRects));
  return CHANNEL_RC_OK;
}
class UnopenedGraphics : public testing::Test {
protected:
  Recorded           recorded;
  Recorder           events  { recorded                  };
  UnjoinedConnection unjoined;
  GraphicsChannel    channel { unjoined.channels, events };
};
// The open fails while no client has joined rdpgfx, but the context and its slots are in place.
class GraphicsSlots : public UnopenedGraphics {
protected:
  GraphicsSlots() {
    Written() = { };
  }
  bool                 opened { channel.Open()                         };
  RdpgfxServerContext& context{ GraphicsChannelProbe::Context(channel) };
};
auto Advertise(std::span<RDPGFX_CAPSET> sets) -> RDPGFX_CAPS_ADVERTISE_PDU {
  return { .capsSetCount = Narrowed<std::uint16_t>(sets.size()), .capsSets = sets.data() };
}
auto At(std::chrono::milliseconds time_of_day) -> std::chrono::system_clock::time_point {
  return std::chrono::sys_days{ std::chrono::year{ 2026 } / 9 / 27 } + time_of_day;
}
constexpr auto V10  = std::to_underlying(GfxVersion::V10);
constexpr auto V101 = std::to_underlying(GfxVersion::V101);
}
TEST_F(UnopenedGraphics, OpeningBeforeTheClientJoinsTheChannelIsRefused) {
  EXPECT_FALSE(channel.Open());
}
TEST_F(UnopenedGraphics, AChannelOpensOnce) {
  std::ignore = channel.Open();
  EXPECT_DEATH(std::ignore = channel.Open(), "graphics opens once");
}
TEST_F(UnopenedGraphics, SendingBeforeOpeningIsAContractFailure) {
  EXPECT_DEATH(std::ignore = channel.EndFrame(1), "the graphics channel is open");
}
TEST_F(GraphicsSlots, AShortCapabilitySetIsDropped) {
  std::array<RDPGFX_CAPSET, 3> sets{ { { .version = V101, .length = 4, .flags = 0    },
                                       { .version = V10 , .length = 3, .flags = 0    },
                                       { .version = V10 , .length = 4, .flags = 0x20 } } };
  auto const                   pdu { Advertise(sets) };
  EXPECT_EQ(context.CapsAdvertise(&context, &pdu), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.advertised, ElementsAre(FieldsAre(GfxVersion::V10, GfxCapsFlags::AvcDisabled)));
}
TEST_F(GraphicsSlots, AVersion101SetCarriesSixteenReservedBytes) {
  std::array<RDPGFX_CAPSET, 1> sets{ { { .version = V101, .length = 16, .flags = 0 } } };
  auto const                   pdu { Advertise(sets)                                   };
  EXPECT_EQ(context.CapsAdvertise(&context, &pdu), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.advertised, ElementsAre(FieldsAre(GfxVersion::V101, GfxCapsFlags{ })));
}
TEST_F(GraphicsSlots, ARefusedAdvertisementIsNotSupported) {
  recorded.accepts = false;
  std::array<RDPGFX_CAPSET, 1> sets{ { { .version = V10, .length = 4, .flags = 0 } } };
  auto const                   pdu { Advertise(sets)                                 };
  EXPECT_EQ(context.CapsAdvertise(&context, &pdu), ERROR_NOT_SUPPORTED);
}
TEST_F(GraphicsSlots, AThrowingHandlerIsAnInternalError) {
  recorded.throws = true;
  std::array<RDPGFX_CAPSET, 1>       sets{ { { .version = V10, .length = 4, .flags = 0 } } };
  auto const                         pdu { Advertise(sets)                                 };
  RDPGFX_FRAME_ACKNOWLEDGE_PDU const ack { .queueDepth = 0, .frameId = 1                   };
  EXPECT_EQ(context.CapsAdvertise(&context, &pdu), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(context.FrameAcknowledge(&context, &ack), ERROR_INTERNAL_ERROR);
  EXPECT_THAT(recorded.failures, ElementsAre("Graphics capabilities", "Graphics frame acknowledgement"));
}
TEST_F(GraphicsSlots, TheAssignedIdReachesTheHandler) {
  EXPECT_TRUE(context.ChannelIdAssigned(&context, 7));
  EXPECT_EQ(recorded.assigned, 7U);
}
TEST_F(GraphicsSlots, AConfirmCarriesTheDataLengthOfItsVersion) {
  context.CapsConfirm = Confirmed;
  EXPECT_TRUE(channel.CapsConfirm({ .version = GfxVersion::V101 }));
  EXPECT_THAT(Written().confirmed, testing::Optional(FieldsAre(V101, 16u, 0u)));
  EXPECT_TRUE(channel.CapsConfirm({ .version = GfxVersion::V10, .flags = GfxCapsFlags::AvcDisabled }));
  EXPECT_THAT(Written().confirmed, testing::Optional(FieldsAre(V10, 4u, 0x20u)));
}
TEST_F(GraphicsSlots, ASuspendedAcknowledgementHasNoQueueDepth) {
  RDPGFX_FRAME_ACKNOWLEDGE_PDU const suspended{ .queueDepth = SUSPEND_FRAME_ACKNOWLEDGEMENT, .frameId = 7 };
  RDPGFX_FRAME_ACKNOWLEDGE_PDU const queued   { .queueDepth = 5, .frameId = 8                             };
  EXPECT_EQ(context.FrameAcknowledge(&context, &suspended), CHANNEL_RC_OK);
  EXPECT_EQ(context.FrameAcknowledge(&context, &queued), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.acks, ElementsAre(FieldsAre(7u, 0u, true), FieldsAre(8u, 5u, false)));
}
TEST_F(GraphicsSlots, AQoeAcknowledgementKeepsItsFields) {
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const qoe{ .frameId = 1, .timestamp = 2, .timeDiffSE = 3, .timeDiffEDR = 4 };
  EXPECT_EQ(context.QoeFrameAcknowledge(&context, &qoe), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.qoes, ElementsAre(FieldsAre(1u, 2u, 3, 4)));
}
TEST_F(GraphicsSlots, MonitorCornersAreInclusive) {
  context.ResetGraphics = Reset;
  std::array<GraphicsMonitor, 1> const monitors{ { { .area    = { .x = 10, .y = 20, .w = 1920, .h = 1080 },
                                                     .primary = true } } };
  EXPECT_TRUE(channel.ResetGraphics({ .width = 1930, .height = 1100 }, monitors));
  EXPECT_THAT(Written().monitors, ElementsAre(FieldsAre(10, 20, 1929, 1099, std::uint32_t{ MONITOR_PRIMARY })));
}
TEST_F(GraphicsSlots, AnAvc420RegionHasExclusiveCornersAndThePackedQuality) {
  context.SurfaceCommand = Commanded;
  std::array<Rect, 1> const regions{ { { .x = 1, .y = 2, .w = 3, .h = 4 } } };
  GraphicsCommand const     command{ .codec     = GfxCodec::Avc420,
                                     .area      = { .x = 0, .y = 0, .w = 8, .h = 8 },
                                     .metablock = { .regions = regions,
                                                    .quality = { .qp = 26, .progressive = true, .quality = 100 } } };
  EXPECT_TRUE(channel.SurfaceCommand(command));
  EXPECT_TRUE(Written().avc420);
  EXPECT_THAT(Written().regions, ElementsAre(FieldsAre(1, 2, 4, 6)));
  EXPECT_THAT(Written().quality, ElementsAre(FieldsAre(26 | 0x80, 100, 26, 0, 1)));
}
TEST_F(GraphicsSlots, OnlyAnAvc420CommandCarriesAMetablock) {
  context.SurfaceCommand = Commanded;
  GraphicsCommand const command{ .codec = GfxCodec::Planar, .area = { .x = 0, .y = 0, .w = 8, .h = 8 } };
  EXPECT_TRUE(channel.SurfaceCommand(command));
  EXPECT_FALSE(Written().avc420);
}
TEST_F(GraphicsSlots, AnAvc420CommandCarriesItsRegions) {
  GraphicsCommand const command{ .codec = GfxCodec::Avc420, .area = { .x = 0, .y = 0, .w = 1, .h = 1 } };
  EXPECT_DEATH(std::ignore = channel.SurfaceCommand(command), "an AVC420 command carries its regions");
}
TEST_F(GraphicsSlots, OnlyAnAvc420CommandCarriesRegions) {
  std::array<Rect, 1> const regions{ { { .x = 0, .y = 0, .w = 1, .h = 1 } } };
  GraphicsCommand const     command{ .codec     = GfxCodec::Planar,
                                     .area      = { .x = 0, .y = 0, .w = 1, .h = 1 },
                                     .metablock = { .regions = regions } };
  EXPECT_DEATH(std::ignore = channel.SurfaceCommand(command), "only an AVC420 command carries regions");
}
TEST(FrameTimestamp, PacksTheUtcTimeOfDay) {
  EXPECT_EQ(FrameTimestamp(At(0ms)), 0u);
  EXPECT_EQ(FrameTimestamp(At(1h)), 0x00400000u);
  EXPECT_EQ(FrameTimestamp(At(1min)), 0x00010000u);
  EXPECT_EQ(FrameTimestamp(At(1s)), 0x00000400u);
  EXPECT_EQ(FrameTimestamp(At(1ms)), 1u);
  EXPECT_EQ(FrameTimestamp(At(23h + 59min + 59s + 999ms)), 0x05fbefe7u);
  EXPECT_EQ(FrameTimestamp(At(24h)), 0u);
}
}
