#pragma once
#include "graphics-observer.hpp"
#include "test-backend-core.hpp"
#include "test-frame-checks.hpp"
namespace BackendGate {
class GraphicsSession : public testing::Test, protected FrameChecks {
protected:
  void ThenWriteDisconnect(Client& client) {
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    ASSERT_TRUE(freerdp_disconnect(client.Instance().get()));
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_DISCONNECTED; });
    });
    EXPECT_NE(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
    backend.reset();
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "")) << logs.Text(true);
    RecordProperty("trace", logs.Text(true));
  }

  void Open(unsigned w = 640, unsigned h = 480, sdlrdp_aspect aspect = { }, sdlrdp_codec codec = SDLRDP_CODEC_RAW,
            unsigned audio_latency = 0) {
    sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), w, h, 0, Logs::Collect, &logs };
    config.aspect           = aspect;
    config.codec            = codec;
    config.audio_latency_ms = audio_latency;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
    backend.reset(handle);
  }
  Client&                     GraphicsClient() { return *graphics_client; }
  Headless::GraphicsObserver& GraphicsObserver() { return *graphics_observer; }
  void PresentProgressiveDamage(Client& client, std::vector<UINT32> const& pixels, sdlrdp_rect damage) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
    EXPECT_LE(client.MaxError(pixels), 24u);
  }
  void ConnectPipeline(Client& client) {
    client.EnableGraphics();
    Connect(client);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(30)))
        << logs.Text(true);
  }
  void ThenProgressivePicture(Client& client, std::vector<UINT32> const& pixels) {
    Present(pixels, 640, 480);
    ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
    EXPECT_LE(client.MaxError(pixels), 24u);
  }
  void GivenGraphicsClient(sdlrdp_codec codec) {
    Open(640, 480, { }, codec);
    if (::testing::Test::HasFatalFailure()) return;
    graphics_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true, 640, 480);
    graphics_client->EnableGraphics();
  }
  void GivenPipelinedGraphics() {
    Open(320, 200, { }, SDLRDP_CODEC_PROGRESSIVE);
    if (::testing::Test::HasFatalFailure()) return;
    graphics_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
    graphics_client->EnableGraphics();
    graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
    ConnectGraphics(*graphics_client, *graphics_observer);
  }
  void ThenLegacyFallback(Client& client) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    auto events    = EventsUntil(
        [](auto const& events) {
          return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
        },
        false, &client);
    auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
    ASSERT_NE(connected, events.end()) << logs.Text();
    EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
  }
  void PresentMatching(Client& client, std::vector<UINT32> const& pixels) {
    Present(pixels, 640, 480);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  }
  void AwaitFrames(Client& client, auto const& frames, std::size_t count) {
    ASSERT_TRUE(client.Until([&] { return frames.size() == count; }));
  }
  void PresentGraphicsFrames(Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32> const& pixels,
                             unsigned first, unsigned last) {
    std::ranges::for_each(std::views::iota(first, last + 1), [&](unsigned count) {
      Present(pixels, 320, 200);
      if (::testing::Test::HasFatalFailure()) return;
      AwaitFrames(client, observer.Observed().frames, count);
      if (::testing::Test::HasFatalFailure()) return;
    });
  }
  void ConnectGraphics(Client& client, Headless::GraphicsObserver& observer) {
    observer.Observed().automatic = false;
    Connect(client);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(30)))
        << logs.Text(true);
  }
  void Connect(Client& client, bool ack = true) {
    ASSERT_TRUE(
        freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  }
  std::unique_ptr<Client>                     graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
class RoundFive : public GraphicsSession {
protected:
  void ThenPipelinedWindow(Client& client, auto const& frames, std::vector<UINT32> const& pixels) {
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    AwaitFrames(client, frames, 1);
    if (::testing::Test::HasFatalFailure()) return;
    Present(pixels, 320, 200);
    AwaitFrames(client, frames, 2);
    if (::testing::Test::HasFatalFailure()) return;
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  }
  void ThenNeverAcknowledges(auto timed) {
    Open(320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    Client client(sdlrdp_port(backend.get()), true);
    Connect(client);
    FrameObserver const       observer(client);
    std::vector<UINT32> const pixels(320uz * 200, 0x778899);
    timed([&] {
      Present(pixels, 320, 200);
      ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
      ThenAcknowledgementTimeout(pixels);
    });
  }
  void ThenAcknowledgementTimeout(std::vector<UINT32> const& pixels) {
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 0u);
    ThenTimedOutFrames("[0-9]+", 1);
  }
  void ThenTimedOutFrames(std::string_view sent, unsigned minimum) {
    backend.reset();
    auto        text  = logs.Text(true);
    std::smatch match;
    ASSERT_TRUE(std::regex_search(text, match, std::regex(std::format(R"(Frames: {} sent,[^\n]*, ([0-9]+) timed out\.)",
                                                                      sent))))
        << text;
    EXPECT_GE(oxbox::utilities::ParseNumber<unsigned>(match.str(1)), minimum);
  }
  void ThenAspectMouse(Client& client) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 639, 479));
    auto events = Events(1);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].mouse_move.x, 639);
    EXPECT_EQ(events[0].mouse_move.y, 349);
  }
  void ThenAgedWindowResumes(std::vector<UINT32> const& pixels) {
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
    ASSERT_TRUE(GraphicsObserver().AckFrame(4, 0));
    AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 7);
    if (::testing::Test::HasFatalFailure()) return;
    ThenGraphicsTimeoutStatistics();
  }
  void ThenColourDepth(unsigned depth) {
    Client client(sdlrdp_port(backend.get()), false);
    ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth, depth));
    Connect(client, false);
    client.Tolerance(depth == 16 ? 7 : 0);
    EXPECT_EQ(freerdp_settings_get_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth), depth);
    std::vector<UINT32> pixels(320uz * 200);
    std::ranges::generate(pixels, [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  }
  void WhenAcknowledgedFrame(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels, unsigned i,
                             auto wait) {
    Present(pixels, 320, 200);
    auto waiting = std::async(std::launch::async, wait, std::ref(*backend));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == i; }));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(waiting.get(), 1);
    EXPECT_TRUE(std::ranges::none_of(Events(), [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
  }
  void ThenProgressiveDamageCost(Client& client, Headless::GraphicsObserver& observer, uint64_t before) {
    EXPECT_EQ(observer.Observed().progressive_headers, 1u);
    EXPECT_EQ(observer.Observed().surfaces.size(), 1u);
    EXPECT_LT(client.Received() - before, 4096u);
    ThenQoe(client, observer);
  }
  void ThenAutoChangesToRaw(Client& client, std::vector<UINT32>& pixels) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
    pixels = GraphicsScene(4, false);
    Present(pixels, 640, 480);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }));
    auto events  = Events();
    auto changed = std::ranges::find(events, SDLRDP_CODEC_CHANGED, &sdlrdp_event::type);
    ASSERT_NE(changed, events.end());
    EXPECT_EQ(changed->codec_changed.codec, SDLRDP_CODEC_RAW);
    RecordProperty("trace",
                   "auto connects as progressive; live raw preference produces exact RGB and CODEC_CHANGED raw");
  }
  void ThenGraphicsTimeoutStatistics() {
    ThenTimedOutFrames("7", 2);
    EXPECT_EQ(GraphicsObserver().Observed().frames.size(), 7u);
  }
  void ThenGraphicsAcknowledgementsCounted() {
    ASSERT_TRUE(GraphicsObserver().Ack());
    ASSERT_TRUE(GraphicsClient().Until([&] { return Acknowledged(); }));
    EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 3u);
  }
  void ThenGraphicsWindowReleases(std::vector<UINT32> const& pixels) {
    ASSERT_TRUE(GraphicsObserver().AckFrame(0, 0));
    ASSERT_TRUE(GraphicsClient().Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  }
  void ThenLegacyWindowReleases(Client& client, FrameObserver const& observer, std::vector<UINT32> const& pixels) {
    auto* update = client.Instance()->context->update;
    ASSERT_TRUE(update->SurfaceFrameAcknowledge(update->context, observer.Frames().front()));
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  }
};

}
