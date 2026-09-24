#include <sdl-rdp/headless-client.test/gate.hpp>
#include <sdl-rdp/headless-client.test/graphics-observer.hpp>
#include <sdl-rdp/headless-client.test/mode.hpp>
#include <sdl-rdp/headless-client.test/peer-status.hpp>
#include <sdl-rdp/headless-client.test/round-five.hpp>
#include <sdl-rdp/video/graphics-link.hpp>

namespace BackendGate {
class GraphicsGate : public Gate {
protected:
  auto ThenFullGraphicsWindow(Headless::GraphicsObserver& observer, sdlrdp_rect full) -> void {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
    EXPECT_EQ(observer.Observed().frames.size(), 2u);
  }
  auto ThenCumulativeAcknowledgement(Client& client, Headless::GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
    ASSERT_TRUE(observer.Ack());
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
    RecordProperty("trace", "two unacknowledged frames exhaust the window; suspend releases third; resume waits; "
                            "cumulative ack releases wait");
  }
  auto ThenGraphicsTakeover(Client& graphics) -> void {
    Client next(sdlrdp_port(backend.get()), true);
    next.EnableGraphics();
    next.Tolerance(graphics.Tolerance());
    ASSERT_TRUE(freerdp_connect(next.Instance().get()));
    ASSERT_TRUE(next.Until([&] { return next.Matches(pixels); })) << logs.Text(true);
    RecordProperty("trace", "pipeline frame -> legacy takeover frame -> fresh pipeline takeover frame");
  }
  auto ThenLegacyAndGraphicsTakeover(Client& graphics) -> void {
    Client legacy(sdlrdp_port(backend.get()), true);
    legacy.Tolerance(GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0);
    ASSERT_TRUE(freerdp_connect(legacy.Instance().get()));
    ASSERT_TRUE(legacy.Until([&] { return legacy.Matches(pixels); }));
    EXPECT_FALSE(freerdp_settings_get_bool(legacy.Instance()->context->settings, FreeRDP_SupportGraphicsPipeline));
    ThenGraphicsTakeover(graphics);
  }
  auto WhenQueuedGraphics(Client& client, Headless::GraphicsObserver& observer, sdlrdp_rect full) -> void {
    ASSERT_TRUE(observer.AckFrame(0, 10000000));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    auto deadline = Clock::now() + std::chrono::milliseconds(80);
    while (Clock::now() < deadline) ASSERT_TRUE(client.Pump());
    EXPECT_EQ(observer.Observed().frames.size(), 2u);
  }
  auto ThenDecodedGraphics(Client& client, Headless::GraphicsObserver& observer) -> void {
    Frame(client, { 0, 0, 320, 200 });
    RecordProperty("maximum_channel_error", client.MaxError(pixels));
    EXPECT_LE(client.MaxError(pixels), client.Tolerance());
    EXPECT_EQ(observer.Observed().commands, GetParam().codec == SDLRDP_CODEC_PLANAR ? 200u : 1u);
    auto events    = Events(2);
    auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
    ASSERT_NE(connected, events.end());
    EXPECT_EQ(connected->connected.codec, GetParam().codec);
    ThenResized(client, observer);
    if (::testing::Test::HasFatalFailure()) return;
    RecordProperty("trace", logs.Text(true));
  }
  auto ThenSuspensionAcknowledged(Client& client, Headless::GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    ASSERT_TRUE(observer.Ack());
    ASSERT_TRUE(client.Pump(20));
  }
  static auto ThenResizedSurface(Headless::GraphicsObserver const& observer) -> void {
    ASSERT_EQ(observer.Observed().surfaces.size(), 2u);
    EXPECT_EQ(observer.Observed().deleted, 1u);
    EXPECT_EQ(observer.Observed().surfaces.back().width, 352);
    EXPECT_EQ(observer.Observed().surfaces.back().height, 224);
    EXPECT_EQ(observer.Observed().progressive_headers, GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 2u : 0u);
  }
  auto GivenUnacknowledged() -> void {
    graphics_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
    graphics_client->EnableGraphics();
    graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
    ConnectUnacknowledged(*graphics_client, *graphics_observer);
  }
  auto ConnectUnacknowledged(Client& client, Headless::GraphicsObserver& observer) -> void {
    observer.Observed().automatic = false;
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  }
  auto PresentFrames(Client& client, Headless::GraphicsObserver& observer, unsigned first, unsigned last) -> void {
    sdlrdp_rect const full{ 0, 0, 320, 200 };
    std::ranges::for_each(std::views::iota(first, last + 1), [&](unsigned count) {
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
      ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == count; }));
    });
  }
  auto ThenResized(Client& client, Headless::GraphicsObserver& observer) -> void {
    std::vector<UINT32> resized(352uz * 224, 0x0055aaff);
    sdlrdp_rect const   full   { 0, 0, 352, 224 };
    ASSERT_EQ(sdlrdp_present(backend.get(), resized.data(), 352 * 4, 352, 224, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return client.Matches(resized); })) << logs.Text(true);
    EXPECT_EQ(client.Instance()->context->gdi->width, 352);
    ThenResizedSurface(observer);
  }
  auto FillGraphicsWindow(Client& client, Headless::GraphicsObserver& observer, sdlrdp_rect full) -> void {
    for (unsigned count = 1; count <= 2; ++count) {
      std::ranges::fill(pixels, count * 0x00202020u);
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
      ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == count; })) << logs.Text(true);
    }
  }
  auto ThenSuspendedWindow(Client& client, Headless::GraphicsObserver& observer) -> void {
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
    ASSERT_TRUE(observer.Ack(SUSPEND_FRAME_ACKNOWLEDGEMENT));
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == 3; }));
    ThenSuspensionAcknowledged(client, observer);
  }
  std::unique_ptr<Client>                     graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
namespace {
auto RecordDamageCost(Client& client, std::vector<UINT32> const& pixels, uint64_t before) -> void {
  testing::Test::RecordProperty("damage_wire_bytes", std::to_string(client.Received() - before));
  testing::Test::RecordProperty("maximum_channel_error", client.MaxError(pixels));
  testing::Test::RecordProperty("trace", "7x5 damage at 17,19; one progressive header over two frames; "
                                         "QoE timestamp=1234 decode=7 render=9 retained without logging");
}
}
TEST_P(GraphicsGate, DecodesAndResizes) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  client.Tolerance(GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  ASSERT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "GFX advertised"));
  EXPECT_TRUE(logs.Contains("GFX confirmed version=0x000a0701"));
  ThenDecodedGraphics(client, observer);
}
TEST_P(GraphicsGate, AcknowledgementPacingAndSuspend) {
  GivenUnacknowledged();
  if (::testing::Test::HasFatalFailure()) return;
  auto&             client   = *graphics_client;
  auto&             observer = *graphics_observer;
  sdlrdp_rect const full     { 0, 0, 320, 200 };
  FillGraphicsWindow(client, observer, full);
  if (::testing::Test::HasFatalFailure()) return;
  ThenFullGraphicsWindow(observer, full);
  if (::testing::Test::HasFatalFailure()) return;
  ThenSuspendedWindow(client, observer);
  if (::testing::Test::HasFatalFailure()) return;
  PresentFrames(client, observer, 4, 5);
  if (::testing::Test::HasFatalFailure()) return;
  ThenCumulativeAcknowledgement(client, observer);
}
TEST_P(GraphicsGate, QueueDepthThrottlesBytes) {
  GivenUnacknowledged();
  if (::testing::Test::HasFatalFailure()) return;
  PresentFrames(*graphics_client, *graphics_observer, 1, 2);
  auto& client   = *graphics_client;
  auto& observer = *graphics_observer;
  if (::testing::Test::HasFatalFailure()) return;
  WhenQueuedGraphics(client, observer, { 0, 0, 320, 200 });
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() == 3; }));
  RecordProperty(
      "trace",
      "ack frame 1 with 10000000 queued bytes holds frame 3 despite one free frame slot; queueDepth=0 releases it");
}
TEST_P(GraphicsGate, RejectedChannelUsesLegacy) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::DisplayClient const display(client);
  client.Tolerance(GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX channel rejected"); })) << logs.Text(true);
  Frame(client, { 0, 0, 320, 200 });
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  RecordProperty("trace",
                 "GCC negotiates GFX; client registers only disp; graphics DVC is rejected; legacy frame decodes");
}
TEST_P(GraphicsGate, TakeoverWithLegacy) {
  Client graphics(sdlrdp_port(backend.get()), true);
  graphics.EnableGraphics();
  graphics.Tolerance(GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0);
  ASSERT_TRUE(freerdp_connect(graphics.Instance().get()));
  ASSERT_TRUE(graphics.Until([&] { return logs.Contains("GFX confirmed"); }));
  Frame(graphics, { 0, 0, 320, 200 });
  ThenLegacyAndGraphicsTakeover(graphics);
}
INSTANTIATE_TEST_SUITE_P(Pipeline, GraphicsGate,
                         testing::Values(Mode{ true, SDLRDP_CODEC_PLANAR }, Mode{ true, SDLRDP_CODEC_RAW },
                                         Mode{ true, SDLRDP_CODEC_PROGRESSIVE }),
                         ModeName);

TEST_F(RoundFive, GraphicsAutoUsesProgressive) {
  Open(640, 480, { }, SDLRDP_CODEC_AUTO);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  ConnectPipeline(client);
  if (::testing::Test::HasFatalFailure()) return;
  auto events    = Events();
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_PROGRESSIVE);
  auto pixels = GraphicsScene(3, false);
  ThenProgressivePicture(client, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAutoChangesToRaw(client, pixels);
}

TEST_F(RoundFive, ProgressiveDamageAndQoe) {
  GivenGraphicsClient(SDLRDP_CODEC_PROGRESSIVE);
  if (::testing::Test::HasFatalFailure()) return;
  auto&                      client   = GraphicsClient();
  Headless::GraphicsObserver observer(client);
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto pixels = GraphicsScene(5, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  auto              before = client.Received();
  sdlrdp_rect const damage { 17, 19, 7, 5 };
  std::ranges::for_each(std::views::iota(damage.y, damage.y + damage.h), [&](int y) {
    std::ranges::fill(std::span(pixels).subspan((y * 640) + damage.x, damage.w), 0x00ff0000u);
  });
  PresentProgressiveDamage(client, pixels, damage);
  if (::testing::Test::HasFatalFailure()) return;
  ThenProgressiveDamageCost(client, observer, before);
  RecordDamageCost(client, pixels, before);
}

TEST_F(RoundFive, GraphicsVersion101) {
  GivenGraphicsClient(SDLRDP_CODEC_PROGRESSIVE);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = GraphicsClient();
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_GfxCapsFilter,
                                          ((1u << 11) - 1) & ~(1u << 3)));
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed version=0x000a0100 flags=0x00000000"); }));
  auto pixels = GraphicsScene(3, false);
  ThenProgressivePicture(client, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty(
      "trace",
      "10.1-only advertisement confirms its 16-byte reserved capability data with no flags; progressive decodes");
}

TEST_F(RoundFive, GraphicsWithoutDynamicChannelsUsesLegacy) {
  Open(640, 480, { }, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_SupportGraphicsPipeline, TRUE));
  client.Instance()->LoadChannels = [](freerdp*) -> BOOL { return TRUE; };
  ThenLegacyFallback(client);
  if (::testing::Test::HasFatalFailure()) return;
  {
    auto const status  = RequiredStatus(*backend);
    auto const elapsed = Clock::now() - status.activated_at;
    EXPECT_GE(elapsed, Backend::GraphicsConnectionWait);
    EXPECT_FALSE(status.graphics.has_value());
    EXPECT_FALSE(status.holding);
    RecordProperty("activation_to_legacy_ms",
                   std::to_string(std::chrono::duration<double, std::milli>(elapsed).count()));
  }
  auto pixels = GraphicsScene(4, false);
  PresentMatching(client, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty("trace", "GFX setting on; no DRDYNVC/addin; deadline emits legacy connected; raw frame decodes");
}
TEST_F(RoundFive, GraphicsWithoutCapabilitiesUsesLegacy) {
  GivenGraphicsClient(SDLRDP_CODEC_RAW);
  if (::testing::Test::HasFatalFailure()) return;
  auto&                      client   = GraphicsClient();
  Headless::GraphicsObserver observer(client);
  observer.Observed().advertise = false;
  ThenLegacyFallback(client);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  auto pixels = GraphicsScene(4, false);
  PresentMatching(client, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty("trace", "GFX DVC opened; CapsAdvertise withheld; deadline emits legacy connected; raw frame decodes");
}

TEST_F(RoundFive, GraphicsCodecSwitchPreservesUndamagedTiles) {
  GivenGraphicsClient(SDLRDP_CODEC_RAW);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = GraphicsClient();
  Connect(client);
  auto pixels = GraphicsScene(4, false);
  PresentMatching(client, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_PROGRESSIVE), 0);
  sdlrdp_rect const damage{ 18, 45, 1, 1 };
  pixels[(45 * 640) + 18] = 0x0000ee00;
  PresentProgressiveDamage(client, pixels, damage);
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty("trace",
                 "raw picture; switch to progressive with one pixel of damage; entire decoded picture preserved");
}

}
