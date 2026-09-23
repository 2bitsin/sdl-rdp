#include "_detail/test-backend.hpp"

namespace BackendGate {
class GraphicsGate : public Gate{ };
TEST_P(GraphicsGate, DecodesAndResizes)
{
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  ASSERT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "GFX advertised"));
  EXPECT_TRUE(logs.Contains("GFX confirmed version=0x000a0701"));
  Frame(client, { 0, 0, 320, 200 });
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
  EXPECT_LE(client.MaxError(pixels), client.tolerance);
  EXPECT_EQ(observer.commands, GetParam().codec == SDLRDP_CODEC_PLANAR ? 200u : 1u);
  auto events    = Events(2);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, GetParam().codec);
  std::vector<UINT32> resized(352uz * 224, 0x0055aaff);
  sdlrdp_rect const full{ 0, 0, 352, 224 };
  ASSERT_EQ(sdlrdp_present(backend.get(), resized.data(), 352 * 4, 352, 224, &full, 1), 0);
  ASSERT_TRUE(client.Until([&] { return client.Matches(resized); })) << logs.Text(true);
  EXPECT_EQ(client.instance->context->gdi->width, 352);
  ASSERT_EQ(observer.surfaces.size(), 2u);
  EXPECT_EQ(observer.deleted, 1u);
  EXPECT_EQ(observer.surfaces.back().width, 352);
  EXPECT_EQ(observer.surfaces.back().height, 224);
  EXPECT_EQ(observer.progressive_headers, GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 2u : 0u);
  RecordProperty("trace", logs.Text(true));
}
TEST_P(GraphicsGate, AcknowledgementPacingAndSuspend)
{
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  sdlrdp_rect const full{ 0, 0, 320, 200 };
  for (unsigned count = 1; count <= 2; ++count) {
    std::ranges::fill(pixels, count * 0x00202020u);
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; })) << logs.Text(true);
  }
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  EXPECT_EQ(observer.frames.size(), 2u);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack(SUSPEND_FRAME_ACKNOWLEDGEMENT));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Pump(20));
  for (unsigned count = 4; count <= 5; ++count) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  RecordProperty("trace", "two unacknowledged frames exhaust the window; suspend releases third; resume waits; cumulative ack releases wait");
}
TEST_P(GraphicsGate, QueueDepthThrottlesBytes)
{
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  sdlrdp_rect const full{ 0, 0, 320, 200 };
  for (unsigned count = 1; count <= 2; ++count) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  ASSERT_TRUE(observer.AckFrame(0, 10000000));
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  auto deadline = Clock::now() + std::chrono::milliseconds(80);
  while (Clock::now() < deadline) ASSERT_TRUE(client.Pump());
  EXPECT_EQ(observer.frames.size(), 2u);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  RecordProperty("trace", "ack frame 1 with 10000000 queued bytes holds frame 3 despite one free frame slot; queueDepth=0 releases it");
}
TEST_P(GraphicsGate, RejectedChannelUsesLegacy)
{
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::DisplayClient const display(client);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX channel rejected"); })) << logs.Text(true);
  Frame(client, { 0, 0, 320, 200 });
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  RecordProperty("trace", "GCC negotiates GFX; client registers only disp; graphics DVC is rejected; legacy frame decodes");
}
TEST_P(GraphicsGate, TakeoverWithLegacy)
{
  Client graphics(sdlrdp_port(backend.get()), true);
  graphics.EnableGraphics();
  graphics.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(graphics.instance.get()));
  ASSERT_TRUE(graphics.Until([&] { return logs.Contains("GFX confirmed"); }));
  Frame(graphics, { 0, 0, 320, 200 });
  Client legacy(sdlrdp_port(backend.get()), true);
  legacy.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(legacy.instance.get()));
  ASSERT_TRUE(legacy.Until([&] { return legacy.Matches(pixels); }));
  EXPECT_FALSE(freerdp_settings_get_bool(legacy.instance->context->settings, FreeRDP_SupportGraphicsPipeline));
  Client next(sdlrdp_port(backend.get()), true);
  next.EnableGraphics();
  next.tolerance = graphics.tolerance;
  ASSERT_TRUE(freerdp_connect(next.instance.get()));
  ASSERT_TRUE(next.Until([&] { return next.Matches(pixels); })) << logs.Text(true);
  RecordProperty("trace", "pipeline frame -> legacy takeover frame -> fresh pipeline takeover frame");
}
INSTANTIATE_TEST_SUITE_P(Pipeline, GraphicsGate, testing::Values(Mode{ true, SDLRDP_CODEC_PLANAR }, Mode{ true, SDLRDP_CODEC_RAW }, Mode{ true, SDLRDP_CODEC_PROGRESSIVE }), ModeName);

TEST_F(RoundFive, GraphicsAutoUsesProgressive)
{
  Open(640, 480, {}, SDLRDP_CODEC_AUTO);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto events    = Events();
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_PROGRESSIVE);
  auto pixels = GraphicsScene(3, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }));
  events       = Events();
  auto changed = std::ranges::find(events, SDLRDP_CODEC_CHANGED, &sdlrdp_event::type);
  ASSERT_NE(changed, events.end());
  EXPECT_EQ(changed->codec_changed.codec, SDLRDP_CODEC_RAW);
  RecordProperty("trace", "auto connects as progressive; live raw preference produces exact RGB and CODEC_CHANGED raw");
}

TEST_F(RoundFive, ProgressiveDamageAndQoe)
{
  Open(640, 480, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto pixels = GraphicsScene(5, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  auto              before = client.Received();
  sdlrdp_rect const damage{ 17, 19, 7, 5 };
  for (int y = damage.y; y < damage.y + damage.h; ++y)
    for (int x = damage.x; x < damage.x + damage.w; ++x) pixels[(y * 640) + x] = 0x00ff0000;
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(observer.surfaces.size(), 1u);
  EXPECT_LT(client.Received() - before, 4096u);
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe{ observer.frames.back().frameId, 1234, 7, 9 };
  ASSERT_EQ(observer.channel->QoeFrameAcknowledge(observer.channel, &qoe), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] {
    std::scoped_lock lock(backend->state->session_guard);
    auto const& received = backend->state->current->graphics_qoe;
    return received.timestamp == qoe.timestamp && received.timeDiffSE == 7 && received.timeDiffEDR == 9;
  }));
  EXPECT_FALSE(logs.Contains("GFX QoE"));
  RecordProperty("damage_wire_bytes", std::to_string(client.Received() - before));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
  RecordProperty("trace", "7x5 damage at 17,19; one progressive header over two frames; QoE timestamp=1234 decode=7 render=9 retained without logging");
}

TEST_F(RoundFive, GraphicsVersion101)
{
  Open(640, 480, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_GfxCapsFilter, ((1u << 11) - 1) & ~(1u << 3)));
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed version=0x000a0100 flags=0x00000000"); }));
  auto pixels = GraphicsScene(3, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  RecordProperty("trace", "10.1-only advertisement confirms its 16-byte reserved capability data with no flags; progressive decodes");
}

TEST_F(RoundFive, GraphicsWithoutDynamicChannelsUsesLegacy)
{
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_SupportGraphicsPipeline, TRUE));
  client.instance->LoadChannels = [](freerdp*) -> BOOL { return TRUE; };
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events    = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
  },
                            false, &client);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
  {
    std::scoped_lock const lock(backend->state->session_guard);
    auto const& peer    = *backend->state->current;
    auto        elapsed = Clock::now() - peer.activated_at;
    EXPECT_GE(elapsed, Backend::Peer::GraphicsConnectionWait);
    EXPECT_FALSE(peer.gfx);
    EXPECT_FALSE(peer.connection);
    RecordProperty("activation_to_legacy_ms", std::to_string(std::chrono::duration<double, std::milli>(elapsed).count()));
  }
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  RecordProperty("trace", "GFX setting on; no DRDYNVC/addin; deadline emits legacy connected; raw frame decodes");
}
TEST_F(RoundFive, GraphicsWithoutCapabilitiesUsesLegacy)
{
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.advertise = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events    = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
  },
                            false, &client);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  RecordProperty("trace", "GFX DVC opened; CapsAdvertise withheld; deadline emits legacy connected; raw frame decodes");
}

TEST_F(RoundFive, GraphicsCodecSwitchPreservesUndamagedTiles)
{
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Connect(client);
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_PROGRESSIVE), 0);
  sdlrdp_rect const damage{ 18, 45, 1, 1 };
  pixels[(45 * 640) + 18] = 0x0000ee00;
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  RecordProperty("trace", "raw picture; switch to progressive with one pixel of damage; entire decoded picture preserved");
}

TEST_F(RoundFive, PipelinedLegacyPresent)
{
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver             observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  auto* update = client.instance->context->update;
  ASSERT_TRUE(update->SurfaceFrameAcknowledge(update->context, observer.ids.front()));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}
TEST_F(RoundFive, PipelinedGraphicsPresent)
{
  Open(320, 200, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 1; }));
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 2; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}

TEST_F(RoundFive, GraphicsFrameStatistics) {
  Open(320, 200, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(320 * 200, 0x123456);
  for (unsigned count = 1; count <= 2; ++count) {
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  for (unsigned count = 0; count < 3; ++count) Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  {
    std::scoped_lock lock(backend->state->frame_guard);
    EXPECT_EQ(backend->state->current->ack_count, 3u);
  }
  freerdp_disconnect(client.instance.get());
  backend.reset();
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Frames: 3 sent, 2 coalesced; encode ")) << logs.Text(true);
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, ([0-9]+) over 100 ms, ([0-9]+) timed out\.)"))) << text;
  EXPECT_EQ(match[1], "0");
  EXPECT_EQ(match[2], "0");
  RecordProperty("statistics", logs.Text(true));
}
TEST_F(RoundFive, GraphicsAcknowledgementsAgeOutAndResume) {
  Open(320, 200, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(320 * 200, 0x123456);
  for (unsigned count = 1; count <= 4; ++count) {
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  ASSERT_TRUE(observer.AckFrame(3, 0));
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) != 0; }));
  for (unsigned count = 5; count <= 6; ++count) {
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.AckFrame(4, 0));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 7; }));
  backend.reset();
  EXPECT_EQ(observer.frames.size(), 7u);
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match,
    std::regex(R"(Frames: 7 sent,[^\n]*, ([0-9]+) timed out\.)"))) << text;
  EXPECT_GE(std::stoull(match[1].str()), 2u);
}
}
