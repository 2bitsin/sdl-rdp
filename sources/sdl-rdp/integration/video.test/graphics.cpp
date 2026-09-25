#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::integration::video_test::detail::graphics {
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::RequiredStatus;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::DisplayClient;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::codec::Gate;
using sdl_rdp::headless_client_test::codec::Mode;
using sdl_rdp::headless_client_test::codec::ModeName;
using sdl_rdp::headless_client_test::frame::GraphicsScene;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::video::GraphicsConnectionWait;

class GraphicsGate : public Gate {
protected:
  auto ThenFullGraphicsWindow(GraphicsObserver& observer, sdlrdp_rect full) -> void {
    ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
    EXPECT_EQ(sdlrdp_wait_frame(&*backend, 1), 0);
    EXPECT_EQ(observer.Observed().frames.size(), 2u);
  }
  auto ThenCumulativeAcknowledgement(Client& client, GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(&*backend, 0), 0);
    ASSERT_TRUE(observer.Ack());
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(&*backend, 0) == 1; }));
    RecordProperty("trace", "two unacknowledged frames exhaust the window; suspend releases third; resume waits; "
                            "cumulative ack releases wait");
  }
  auto ThenGraphicsTakeover(Client& graphics) -> void {
    Client next(sdlrdp_port(&*backend), true);
    next.EnableGraphics();
    next.Tolerance(graphics.Tolerance());
    ASSERT_TRUE(next.Connect());
    ASSERT_TRUE(next.Until([&] { return next.Matches(pixels); })) << logs.Text(true);
    RecordProperty("trace", "pipeline frame -> legacy takeover frame -> fresh pipeline takeover frame");
  }
  auto ThenLegacyAndGraphicsTakeover(Client& graphics) -> void {
    Client legacy(sdlrdp_port(&*backend), true);
    legacy.Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
    ASSERT_TRUE(legacy.Connect());
    ASSERT_TRUE(legacy.Until([&] { return legacy.Matches(pixels); }));
    EXPECT_FALSE(freerdp_settings_get_bool(legacy.Instance()->context->settings, FreeRDP_SupportGraphicsPipeline));
    ThenGraphicsTakeover(graphics);
  }
  auto WhenQueuedGraphics(Client& client, GraphicsObserver& observer, sdlrdp_rect full) -> void {
    ASSERT_TRUE(observer.AckFrame(0, 10000000));
    ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
    auto deadline = Clock::now() + std::chrono::milliseconds(80);
    while (Clock::now() < deadline) ASSERT_TRUE(client.Pump());
    EXPECT_EQ(observer.Observed().frames.size(), 2u);
  }
  auto ThenDecodedGraphics(Client& client, GraphicsObserver& observer) -> void {
    ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
    RecordProperty("maximum_channel_error", client.MaxError(pixels));
    EXPECT_LE(client.MaxError(pixels), client.Tolerance());
    EXPECT_EQ(observer.Observed().commands, GetParam().codec == SDLRDP_CODEC_PLANAR ? 200u : 1u);
    ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, GetParam().codec));
    ASSERT_NO_FATAL_FAILURE(ThenResized(client, observer));
    RecordProperty("trace", logs.Text(true));
  }
  auto ThenSuspensionAcknowledged(Client& client, GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(&*backend, 0), 1);
    ASSERT_TRUE(observer.Ack());
    ASSERT_TRUE(client.Pump(20));
  }
  static auto ThenResizedSurface(GraphicsObserver const& observer) -> void {
    ASSERT_EQ(observer.Observed().surfaces.size(), 2u);
    EXPECT_EQ(observer.Observed().deleted, 1u);
    EXPECT_EQ(observer.Observed().surfaces.back().width, 352);
    EXPECT_EQ(observer.Observed().surfaces.back().height, 224);
    EXPECT_EQ(observer.Observed().progressive_headers, GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 2u : 0u);
  }
  auto GivenUnacknowledged() -> void {
    graphics_client = std::make_unique<Client>(sdlrdp_port(&*backend), true);
    graphics_client->EnableGraphics();
    graphics_observer = std::make_unique<GraphicsObserver>(*graphics_client);
    ConnectUnacknowledged(*graphics_client, *graphics_observer);
  }
  auto ConnectUnacknowledged(Client& client, GraphicsObserver& observer) -> void {
    observer.Observed().automatic = false;
    ASSERT_TRUE(client.Connect());
    ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  }
  auto PresentFrames(Client& client, GraphicsObserver& observer, std::uint32_t first, std::uint32_t last) -> void {
    sdlrdp_rect const full{ 0, 0, 320, 200 };
    std::ranges::for_each(std::views::iota(first, last + 1), [&](std::size_t count) {
      ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
      ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == count; }));
    });
  }
  auto ThenResized(Client& client, GraphicsObserver& observer) -> void {
    Pixels            resized(352uz * 224, 0x0055aaff);
    sdlrdp_rect const full   { 0, 0, 352, 224 };
    ASSERT_EQ(backend.Present(resized, 352, 224, full), 0);
    ASSERT_TRUE(client.Until([&] { return client.Matches(resized); })) << logs.Text(true);
    EXPECT_EQ(client.Instance()->context->gdi->width, 352);
    ThenResizedSurface(observer);
  }
  auto FillGraphicsWindow(Client& client, GraphicsObserver& observer, sdlrdp_rect full) -> void {
    for (std::size_t count = 1; count <= 2; ++count) {
      std::ranges::fill(pixels, count * 0x00202020u);
      ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
      ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == count; })) << logs.Text(true);
    }
  }
  auto ThenSuspendedWindow(Client& client, GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(&*backend, 0), 0);
    ASSERT_TRUE(observer.Ack(SUSPEND_FRAME_ACKNOWLEDGEMENT));
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == 3; }));
    ThenSuspensionAcknowledged(client, observer);
  }
  std::unique_ptr<Client>           graphics_client;
  std::unique_ptr<GraphicsObserver> graphics_observer;
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
  Client client(sdlrdp_port(&*backend), true);
  client.EnableGraphics();
  GraphicsObserver observer(client);
  client.Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  ASSERT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "GFX advertised"));
  EXPECT_TRUE(logs.Contains("GFX confirmed version=0x000a0701"));
  ThenDecodedGraphics(client, observer);
}
TEST_P(GraphicsGate, AcknowledgementPacingAndSuspend) {
  ASSERT_NO_FATAL_FAILURE(GivenUnacknowledged());
  auto&             client   = *graphics_client;
  auto&             observer = *graphics_observer;
  sdlrdp_rect const full     { 0, 0, 320, 200 };
  ASSERT_NO_FATAL_FAILURE(FillGraphicsWindow(client, observer, full));
  ASSERT_NO_FATAL_FAILURE(ThenFullGraphicsWindow(observer, full));
  ASSERT_NO_FATAL_FAILURE(ThenSuspendedWindow(client, observer));
  ASSERT_NO_FATAL_FAILURE(PresentFrames(client, observer, 4, 5));
  ThenCumulativeAcknowledgement(client, observer);
}
TEST_P(GraphicsGate, QueueDepthThrottlesBytes) {
  ASSERT_NO_FATAL_FAILURE(GivenUnacknowledged());
  ASSERT_NO_FATAL_FAILURE(PresentFrames(*graphics_client, *graphics_observer, 1, 2));
  auto& client   = *graphics_client;
  auto& observer = *graphics_observer;
  ASSERT_NO_FATAL_FAILURE(WhenQueuedGraphics(client, observer, { 0, 0, 320, 200 }));
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == 3; }));
  RecordProperty(
      "trace",
      "ack frame 1 with 10000000 queued bytes holds frame 3 despite one free frame slot; queueDepth=0 releases it");
}
TEST_P(GraphicsGate, RejectedChannelUsesLegacy) {
  Client client(sdlrdp_port(&*backend), true);
  client.EnableGraphics();
  DisplayClient const display(client);
  client.Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX channel rejected"); })) << logs.Text(true);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  RecordProperty("trace",
                 "GCC negotiates GFX; client registers only disp; graphics DVC is rejected; legacy frame decodes");
}
TEST_P(GraphicsGate, TakeoverWithLegacy) {
  Client graphics(sdlrdp_port(&*backend), true);
  graphics.EnableGraphics();
  graphics.Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
  ASSERT_TRUE(graphics.Connect());
  ASSERT_TRUE(graphics.Until([&] { return logs.Contains("GFX confirmed"); }));
  ASSERT_NO_FATAL_FAILURE(Frame(graphics, { 0, 0, 320, 200 }));
  ThenLegacyAndGraphicsTakeover(graphics);
}
INSTANTIATE_TEST_SUITE_P(Pipeline, GraphicsGate,
                         testing::Values(Mode{ true, SDLRDP_CODEC_PLANAR }, Mode{ true, SDLRDP_CODEC_RAW },
                                         Mode{ true, SDLRDP_CODEC_PROGRESSIVE }),
                         ModeName);

TEST_F(RoundFive, GraphicsAutoUsesProgressive) {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, SDLRDP_CODEC_AUTO));
  Client client(sdlrdp_port(&*backend), true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(ConnectPipeline(client));
  ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, SDLRDP_CODEC_PROGRESSIVE));
  auto pixels = GraphicsScene(3, false);
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, { 0, 0, 640, 480 }));
  ThenAutoChangesToRaw(client, pixels);
}

TEST_F(RoundFive, ProgressiveDamageAndQoe) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(SDLRDP_CODEC_PROGRESSIVE));
  auto&            client   = GraphicsClient();
  GraphicsObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto pixels = GraphicsScene(5, false);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(client, backend, logs));
  auto              before = client.Received();
  sdlrdp_rect const damage { 17, 19, 7, 5 };
  std::ranges::for_each(std::views::iota(damage.y, damage.y + damage.h), [&](int y) {
    std::ranges::fill(std::span(pixels).subspan((y * 640) + damage.x, damage.w), 0x00ff0000u);
  });
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, damage));
  ASSERT_NO_FATAL_FAILURE(ThenProgressiveDamageCost(client, observer, before));
  RecordDamageCost(client, pixels, before);
}

TEST_F(RoundFive, GraphicsVersion101) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(SDLRDP_CODEC_PROGRESSIVE));
  auto& client = GraphicsClient();
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_GfxCapsFilter,
                                          ((1u << 11) - 1) & ~(1u << 3)));
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed version=0x000a0100 flags=0x00000000"); }));
  auto pixels = GraphicsScene(3, false);
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, { 0, 0, 640, 480 }));
  RecordProperty(
      "trace",
      "10.1-only advertisement confirms its 16-byte reserved capability data with no flags; progressive decodes");
}

TEST_F(RoundFive, GraphicsWithoutDynamicChannelsUsesLegacy) {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, SDLRDP_CODEC_RAW));
  Client client(sdlrdp_port(&*backend), true, 640, 480);
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
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(SDLRDP_CODEC_RAW));
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
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(SDLRDP_CODEC_RAW));
  auto& client = GraphicsClient();
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  auto pixels = GraphicsScene(4, false);
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
  ASSERT_EQ(sdlrdp_set_codec(&*backend, SDLRDP_CODEC_PROGRESSIVE), 0);
  sdlrdp_rect const damage{ 18, 45, 1, 1 };
  pixels[(45 * 640) + 18] = 0x0000ee00;
  ASSERT_NO_FATAL_FAILURE(PresentProgressiveDamage(client, pixels, damage));
  RecordProperty("trace",
                 "raw picture; switch to progressive with one pixel of damage; entire decoded picture preserved");
}

}
