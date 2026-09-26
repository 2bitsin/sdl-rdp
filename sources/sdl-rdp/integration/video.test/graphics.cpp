#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/backend/waits.hpp>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace sdl_rdp::integration::video_test::detail::graphics {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::RequiredStatus;
using sdl_rdp::headless_client_test::backend::UntilLogged;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::DisplayClient;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::codec::Gate;
using sdl_rdp::headless_client_test::codec::Mode;
using sdl_rdp::headless_client_test::codec::ModeName;
using sdl_rdp::headless_client_test::frame::FillArea;
using sdl_rdp::headless_client_test::frame::GraphicsScene;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::GraphicsConnectionWait;

class GraphicsGate : public Gate {
protected:
  auto GivenGraphics(bool acknowledging = true) -> void {
    client = std::make_unique<Client>(backend.Port(), true);
    client->EnableGraphics();
    client->Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
    observer                       = std::make_unique<GraphicsObserver>(*client);
    observer->Observed().automatic = acknowledging;
    ASSERT_TRUE(client->Connect());
    ASSERT_TRUE(UntilLogged(*client, logs, "GFX confirmed")) << logs.Text(true);
  }
  auto ThenFullGraphicsWindow(Rect full) -> void {
    backend.Present(pixels, 320, 200, full);
    EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 1 }));
    EXPECT_EQ(observer->Observed().frames.size(), 2u);
  }
  auto ThenCumulativeAcknowledgement() -> void {
    EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
    ASSERT_TRUE(observer->Ack());
    ASSERT_TRUE(client->Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }); }));
    RecordProperty("trace", "two unacknowledged frames exhaust the window; suspend releases third; resume waits; "
                            "cumulative ack releases wait");
  }
  auto ThenGraphicsTakeover() -> void {
    Client next(backend.Port(), true);
    next.EnableGraphics();
    next.Tolerance(client->Tolerance());
    ASSERT_TRUE(next.Connect());
    ASSERT_TRUE(UntilMatches(next, pixels)) << logs.Text(true);
    RecordProperty("trace", "pipeline frame -> legacy takeover frame -> fresh pipeline takeover frame");
  }
  auto ThenLegacyAndGraphicsTakeover() -> void {
    Client legacy(backend.Port(), true);
    legacy.Tolerance(client->Tolerance());
    ASSERT_TRUE(legacy.Connect());
    ASSERT_TRUE(UntilMatches(legacy, pixels));
    EXPECT_FALSE(freerdp_settings_get_bool(legacy.Instance()->context->settings, FreeRDP_SupportGraphicsPipeline));
    ThenGraphicsTakeover();
  }
  auto WhenQueuedGraphics(Rect full) -> void {
    ASSERT_TRUE(observer->AckFrame(0, 10000000));
    backend.Present(pixels, 320, 200, full);
    auto deadline = Clock::now() + std::chrono::milliseconds(80);
    while (Clock::now() < deadline) ASSERT_TRUE(client->Pump());
    EXPECT_EQ(observer->Observed().frames.size(), 2u);
  }
  auto ThenDecodedGraphics() -> void {
    ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
    RecordProperty("maximum_channel_error", client->MaxError(pixels));
    EXPECT_LE(client->MaxError(pixels), client->Tolerance());
    EXPECT_EQ(observer->Observed().commands, GetParam().codec == Codec::Planar ? 200u : 1u);
    ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(*client, GetParam().codec));
    ASSERT_NO_FATAL_FAILURE(ThenResized());
    RecordProperty("trace", logs.Text(true));
  }
  auto ThenSuspensionAcknowledged() -> void {
    EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
    ASSERT_TRUE(observer->Ack());
    ASSERT_TRUE(client->Pump(20));
  }
  auto ThenResizedSurface() -> void {
    ASSERT_EQ(observer->Observed().surfaces.size(), 2u);
    EXPECT_EQ(observer->Observed().deleted, 1u);
    EXPECT_EQ(observer->Observed().surfaces.back().width, 352);
    EXPECT_EQ(observer->Observed().surfaces.back().height, 224);
    EXPECT_EQ(observer->Observed().progressive_headers, GetParam().codec == Codec::Progressive ? 2u : 0u);
  }
  auto PresentFrames(std::uint32_t first, std::uint32_t last) -> void {
    Rect const full{ .x = 0, .y = 0, .w = 320, .h = 200 };
    std::ranges::for_each(std::views::iota(first, last + 1), [&](std::size_t count) {
      backend.Present(pixels, 320, 200, full);
      ASSERT_TRUE(client->Until([&] { return observer->Observed().frames.size() == count; }));
    });
  }
  auto ThenResized() -> void {
    Pixels     resized(352uz * 224, 0x0055aaff);
    Rect const full   { .x = 0, .y = 0, .w = 352, .h = 224 };
    backend.Present(resized, 352, 224, full);
    ASSERT_TRUE(UntilMatches(*client, resized)) << logs.Text(true);
    EXPECT_EQ(client->DesktopSize(), (Extent{ .width = 352, .height = 224 }));
    ThenResizedSurface();
  }
  auto FillGraphicsWindow(Rect full) -> void {
    for (std::size_t count = 1; count <= 2; ++count) {
      std::ranges::fill(pixels, count * 0x00202020u);
      backend.Present(pixels, 320, 200, full);
      ASSERT_TRUE(client->Until([&] { return observer->Observed().frames.size() == count; })) << logs.Text(true);
    }
  }
  auto ThenSuspendedWindow() -> void {
    EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
    ASSERT_TRUE(observer->Ack(SUSPEND_FRAME_ACKNOWLEDGEMENT));
    ASSERT_TRUE(client->Until([&] { return observer->Observed().frames.size() == 3; }));
    ThenSuspensionAcknowledged();
  }
  std::unique_ptr<GraphicsObserver> observer;
};
namespace {
auto RecordDamageCost(Client& client, Pixels const& pixels, std::uint64_t before) -> void {
  testing::Test::RecordProperty("damage_wire_bytes", std::to_string(client.Received() - before));
  testing::Test::RecordProperty("maximum_channel_error", client.MaxError(pixels));
  testing::Test::RecordProperty("trace", "7x5 damage at 17,19; one progressive header over two frames; "
                                         "QoE timestamp=1234 decode=7 render=9 retained without logging");
}
}
TEST_P(GraphicsGate, DecodesAndResizes) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics());
  ASSERT_TRUE(logs.Contains(LogLevel::Info, "GFX advertised"));
  EXPECT_TRUE(logs.Contains("GFX confirmed version=0x000a0701"));
  ThenDecodedGraphics();
}
TEST_P(GraphicsGate, AcknowledgementPacingAndSuspend) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(false));
  Rect const full{ .x = 0, .y = 0, .w = 320, .h = 200 };
  ASSERT_NO_FATAL_FAILURE(FillGraphicsWindow(full));
  ASSERT_NO_FATAL_FAILURE(ThenFullGraphicsWindow(full));
  ASSERT_NO_FATAL_FAILURE(ThenSuspendedWindow());
  ASSERT_NO_FATAL_FAILURE(PresentFrames(4, 5));
  ThenCumulativeAcknowledgement();
}
TEST_P(GraphicsGate, QueueDepthThrottlesBytes) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(false));
  ASSERT_NO_FATAL_FAILURE(PresentFrames(1, 2));
  ASSERT_NO_FATAL_FAILURE(WhenQueuedGraphics({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  ASSERT_TRUE(observer->AckFrame(0, 0));
  ASSERT_TRUE(client->Until([&] { return observer->Observed().frames.size() == 3; }));
  RecordProperty(
      "trace",
      "ack frame 1 with 10000000 queued bytes holds frame 3 despite one free frame slot; queueDepth=0 releases it");
}
TEST_P(GraphicsGate, RejectedChannelUsesLegacy) {
  client = std::make_unique<Client>(backend.Port(), true);
  client->EnableGraphics();
  DisplayClient const display(*client);
  client->Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
  ASSERT_TRUE(client->Connect());
  ASSERT_TRUE(UntilLogged(*client, logs, "GFX channel rejected")) << logs.Text(true);
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  RecordProperty("trace",
                 "GCC negotiates GFX; client registers only disp; graphics DVC is rejected; legacy frame decodes");
}
TEST_P(GraphicsGate, TakeoverWithLegacy) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics());
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  ThenLegacyAndGraphicsTakeover();
}
INSTANTIATE_TEST_SUITE_P(Pipeline, GraphicsGate,
                         testing::Values(Mode{ .surface = true, .codec = Codec::Planar },
                                         Mode{ .surface = true, .codec = Codec::Raw    },
                                         Mode{ .surface = true, .codec = Codec::Progressive }),
                         ModeName);

TEST_F(RoundFive, GraphicsAutoUsesProgressive) {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, Codec::Auto));
  Client client(backend.Port(), true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(ConnectPipeline(client));
  ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, Codec::Progressive));
  auto pixels = GraphicsScene(3, false);
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, { .x = 0, .y = 0, .w = 640, .h = 480 }));
  ThenAutoChangesToRaw(client, pixels);
}

TEST_F(RoundFive, ProgressiveDamageAndQoe) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(Codec::Progressive));
  auto&            client   = GraphicsClient();
  GraphicsObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(UntilLogged(client, logs, "GFX confirmed"));
  auto pixels = GraphicsScene(5, false);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(client, backend, logs));
  auto       before = client.Received();
  Rect const damage { .x = 17, .y = 19, .w = 7, .h = 5 };
  FillArea(pixels, 640, damage, 0x00ff0000u);
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, damage));
  ASSERT_NO_FATAL_FAILURE(ThenProgressiveDamageCost(client, observer, before));
  RecordDamageCost(client, pixels, before);
}

TEST_F(RoundFive, GraphicsVersion101) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(Codec::Progressive));
  auto& client = GraphicsClient();
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_GfxCapsFilter,
                                          ((1u << 11) - 1) & ~(1u << 3)));
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(UntilLogged(client, logs, "GFX confirmed version=0x000a0100 flags=0x00000000"));
  auto pixels = GraphicsScene(3, false);
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, { .x = 0, .y = 0, .w = 640, .h = 480 }));
  RecordProperty(
      "trace",
      "10.1-only advertisement confirms its 16-byte reserved capability data with no flags; progressive decodes");
}

TEST_F(RoundFive, GraphicsWithoutDynamicChannelsUsesLegacy) {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, Codec::Raw));
  Client client(backend.Port(), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_SupportGraphicsPipeline, true));
  // abi: pLoadChannels, BOOL is int
  client.Instance()->LoadChannels = [](freerdp*) -> int { return true; };
  ASSERT_NO_FATAL_FAILURE(ThenLegacyFallback(client));
  {
    auto const status  = RequiredStatus(*backend);
    auto const elapsed = Clock::now() - status.activated_at;
    EXPECT_GE(elapsed, GraphicsConnectionWait);
    EXPECT_FALSE(status.graphics.has_value());
    EXPECT_FALSE(status.holding);
    RecordProperty("activation_to_legacy_ms",
                   std::to_string(std::chrono::duration<double, std::milli>(elapsed).count()));
  }
  auto pixels = GraphicsScene(4, false);
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
  RecordProperty("trace", "GFX setting on; no DRDYNVC/addin; deadline emits legacy connected; raw frame decodes");
}
TEST_F(RoundFive, GraphicsWithoutCapabilitiesUsesLegacy) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(Codec::Raw));
  auto&            client   = GraphicsClient();
  GraphicsObserver observer(client);
  observer.Observed().advertise = false;
  ASSERT_NO_FATAL_FAILURE(ThenLegacyFallback(client));
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  auto pixels = GraphicsScene(4, false);
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
  RecordProperty("trace", "GFX DVC opened; CapsAdvertise withheld; deadline emits legacy connected; raw frame decodes");
}

TEST_F(RoundFive, GraphicsCodecSwitchPreservesUndamagedTiles) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(Codec::Raw));
  auto& client = GraphicsClient();
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  auto pixels = GraphicsScene(4, false);
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
  (*backend).Presentation().SetCodec(Codec::Progressive);
  Rect const damage{ .x = 18, .y = 45, .w = 1, .h = 1 };
  pixels[(45 * 640) + 18] = 0x0000ee00;
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, damage));
  RecordProperty("trace",
                 "raw picture; switch to progressive with one pixel of damage; entire decoded picture preserved");
}

}
