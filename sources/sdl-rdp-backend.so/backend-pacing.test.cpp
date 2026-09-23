#include <cstddef>
#include <algorithm>

#include "_detail/test-backend.hpp"

namespace BackendGate {
TEST_F(RoundFive, DelayedAcknowledgements)
{
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver       observer(client);
  std::vector<UINT32> pixels(640uz * 480, 0x112233);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  observer.ids.clear(); // Test the negotiated window after ACK support is established.
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  std::ranges::fill(pixels, 0x223344);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
  for (unsigned i = 0; i < 10; ++i) {
    std::ranges::fill(pixels, 0x334455 + i);
    Present(pixels, 640, 480);
  }
  EXPECT_EQ(observer.ids.size(), 2u);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 3; }));
  EXPECT_TRUE(client.Matches(pixels));
  ASSERT_TRUE(observer.Ack());
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_TRUE(observer.coherent);
}
TEST_F(RoundFive, SuppressOutput)
{
  Open();
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client, false);
  FrameObserver observer(client);
  auto* update = client.instance->context->update;
  ASSERT_TRUE(update->SuppressOutput(client.instance->context, 0, nullptr));
  ASSERT_EQ(Events(2).size(), 2u);
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  auto suppressed = Events(1); // Input follows SuppressOutput on the same connection.
  ASSERT_EQ(suppressed.size(), 1u);
  ASSERT_EQ(suppressed.front().type, SDLRDP_KEY);
  auto bytes = client.Received();
  std::vector<UINT32> pixels(640uz * 480, 0x123456);
  Present(pixels, 640, 480);
  std::ranges::fill(pixels, 0x654321);
  Present(pixels, 640, 480);
  // Probe for forbidden output after the ordered suppression barrier. No
  // required event or minimum amount of work depends on this observation span.
  for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(client.Pump(5));
  EXPECT_EQ(client.Received(), bytes);
  EXPECT_TRUE(observer.ids.empty());
  RECTANGLE_16 const area{ 0, 0, 639, 479 };
  ASSERT_TRUE(update->SuppressOutput(client.instance->context, 1, &area));
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  EXPECT_TRUE(client.Matches(pixels));
}
TEST_F(RoundFive, AspectAndMouse)
{
  Open(640, 350, { 4, 3 });
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client, false);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].connected.screen_width, 1024u);
  EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[1].screen.height, 768u);
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  EXPECT_EQ(client.instance->context->gdi->height, 480);
  FrameObserver       observer(client);
  std::vector<UINT32> pixels(640uz * 350);
  std::fill_n(pixels.begin() + 175uz * 640, 640, 0xffffff);
  Present(pixels, 640, 350);
  ASSERT_TRUE(client.Until([&] { return !observer.ids.empty(); }));
  auto* actual    = reinterpret_cast<UINT32*>(client.instance->context->gdi->primary_buffer);
  auto  rows      = std::views::iota(0, 480);
  auto  brightest = std::ranges::max_element(rows, {}, [&](int y) { return actual[static_cast<std::ptrdiff_t>(y) * 640] & 255; });
  EXPECT_LE(std::abs(*brightest - 240), 1);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].mouse_move.x, 639);
  EXPECT_EQ(events[0].mouse_move.y, 349);
  ASSERT_EQ(sdlrdp_set_aspect(backend.get(), { 0, 0 }), 0);
  ASSERT_TRUE(client.Until([&] { return client.instance->context->gdi->height == 350 && client.Matches(pixels); }));
  EXPECT_EQ(client.instance->context->gdi->width, 640);
}
TEST_F(RoundFive, SparseRegions)
{
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC }) {
    Open(1024, 768, {}, codec);
    Client client(sdlrdp_port(backend.get()), true, 1024, 768);
    Connect(client, false);
    FrameObserver       observer(client);
    std::vector<UINT32> pixels(1024uz * 768);
    std::ranges::generate(pixels, [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
    auto bytes = client.Received();
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
    auto bounding = client.Received() - bytes;
    RecordProperty("bounding_bytes_" + std::to_string(codec), std::to_string(bounding));
    bytes = client.Received();
    std::array<sdlrdp_rect, 2> damage{
      { { .x = 0, .y = 0, .w = 8, .h = 8 }, { .x = 1016, .y = 760, .w = 8, .h = 8 } }
    };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 4096, 1024, 768, damage.data(), 2), 0);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 3; }));
    auto used = client.Received() - bytes;
    RecordProperty("region_bytes_" + std::to_string(codec), std::to_string(used));
    EXPECT_LT(used, bounding / 100);
  }
}

static bool WaitForAcknowledgement(Backend::State& state) {
  Expects(state.current != nullptr, "active peer owns the pending frame");
  std::unique_lock lock(state.frame_guard);
  return state.frame_changed.wait_for(lock, std::chrono::seconds(10), [&] {
    return state.current->acknowledged >= state.presented;
  });
}
TEST_F(RoundFive, WaitWithoutRefreshFeedback) {
  Expects(backend == nullptr, "backend has not opened");
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  ASSERT_EQ(Events(2).size(), 2u);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(320 * 200, 0x445566);
  for (unsigned i = 1; i <= 20; ++i) {
    Present(pixels, 320, 200);
    auto waiting = std::async(std::launch::async, WaitForAcknowledgement, std::ref(*backend->state));
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == i; }));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(waiting.get(), 1);
    for (auto const& event : Events()) EXPECT_NE(event.type, SDLRDP_REFRESH);
  }
}
TEST_F(RoundFive, NeverAcknowledges)
{
  Open(320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver             observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x778899);
  auto start = Clock::now();
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_GE(Clock::now() - start, std::chrono::milliseconds(200));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
}
TEST_F(RoundFive, ColourDepths)
{
  Open(320, 200);
  for (auto depth : { 16u, 24u }) {
    Client client(sdlrdp_port(backend.get()), false);
    ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_ColorDepth, depth));
    Connect(client, false);
    client.tolerance = depth == 16 ? 7 : 0;
    EXPECT_EQ(freerdp_settings_get_uint32(client.instance->context->settings, FreeRDP_ColorDepth), depth);
    std::vector<UINT32> pixels(320uz * 200);
    std::ranges::generate(pixels, [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  }
}

TEST_F(RoundFive, ProducerDoesNotStarveOrTear)
{
  Open(1024, 768);
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver         observer(client);
  std::atomic<unsigned> presents = 0;
  std::jthread          producer([&](const std::stop_token& stop) {
    std::vector<UINT32> pixels(1024uz * 768);
    sdlrdp_rect const area{ 0, 0, 1024, 768 };
    while (!stop.stop_requested()) {
      auto sequence = presents.load() + 1;
      std::fill_n(pixels.begin(), 1024, sequence);
      std::fill_n(pixels.end() - 1024, 1024, sequence);
      Expects(sdlrdp_present(backend.get(), pixels.data(), 4096, 1024, 768, &area, 1) == 0,
                       "concurrent present accepted");
      presents = sequence;
      std::this_thread::yield();
    }
  });
  for (unsigned i = 0; i < 20; ++i) {
    auto before       = presents.load();
    auto acknowledged = observer.ids.size();
    ASSERT_TRUE(client.Until([&] {
      return presents.load() > before && observer.ids.size() > acknowledged;
    })) << "both producer and consumer must progress";
    ASSERT_TRUE(observer.Ack());
  }
  producer.request_stop();
  producer.join();
  std::vector<UINT32> final(1024uz * 768);
  std::fill_n(final.begin(), 1024, presents.load());
  std::fill_n(final.end() - 1024, 1024, presents.load());
  ASSERT_TRUE(client.Until([&] { observer.Ack(); return client.Matches(final); }));
  EXPECT_TRUE(observer.coherent);
  EXPECT_LE(observer.ids.size(), presents.load());
  EXPECT_TRUE(std::ranges::is_sorted(observer.ids));
  RecordProperty("presents", std::to_string(presents.load()));
  RecordProperty("acknowledged_frames", std::to_string(observer.ids.size()));
}

TEST_F(RoundFive, AutoPrefersRemoteFX)
{
  Open(320, 200, {}, SDLRDP_CODEC_AUTO);
  Client const client(sdlrdp_port(backend.get()), true);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events    = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CONNECTED; });
  });
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_REMOTEFX);
}
TEST_F(RoundFive, ExpectedDisconnectLogLevels)
{
  Open();
  auto* peer = WLog_Get("com.freerdp.core.peer");
  for (const auto* name : { "ERRINFO_LOGOFF_BY_USER", "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION", "ERRINFO_RPC_INITIATED_DISCONNECT" }) {
    WLog_Print(peer, WLOG_ERROR, "%s [0x00010000]", name);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, name));
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, name));
  }
  for (auto [category, message] : std::array<std::pair<const char*, const char*>, 9>{
           { { "com.freerdp.core.peer", "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]" },
            { "com.freerdp.core.transport", "BIO_read retries exceeded" },
            { "com.freerdp.core.transport", "BIO_read returned a system error 104: Connection reset by peer" },
            { "com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000068:system library::Connection reset by peer" },
            { "com.freerdp.core.transport", "BIO_write returned a system error 32: Broken pipe" },
            { "com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000020:system library::Broken pipe" },
            { "com.freerdp.core.transport", "BIO_read returned a system error 110: Connection timed out" },
            { "com.freerdp.core.transport", "BIO_read returned a system error 5: Input/output error" },
            { "com.freerdp.core", "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]" } }
  }) {
    WLog_Print(WLog_Get(category), WLOG_ERROR, "%s", message);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, message));
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, message));
  }
  const auto* failure = "BIO_write returned a system error 5: Input/output error";
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "%s", failure);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, failure));
  WLog_Print(peer, WLOG_ERROR, "%s", "BIO_read returned a system error 110: Connection timed out");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "BIO_read returned a system error 110"));
  WLog_Print(peer, WLOG_ERROR, "transport failure marker");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "transport failure marker"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "ERRINFO_LOGOFF_BY_USER [0x0001000C]");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "ERRINFO_LOGOFF_BY_USER"));
  RecordProperty("trace", logs.Text(true));
}

TEST_F(RoundFive, GraphicsDisconnectDuringWrite)
{
  constexpr unsigned width  = 2048;
  constexpr unsigned height = 2048;
  Open(width, height, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, width, height);
  client.EnableGraphics();
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  Events();
  std::vector<UINT32> pixels(static_cast<std::size_t>(width) * height);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, width, height));
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  UINT32 value = 1;
  for (auto& pixel : pixels) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    pixel = value & 0xffffff;
  }
  ASSERT_NO_FATAL_FAILURE(Present(pixels, width, height));
  std::array<HANDLE, 64> handles{ };
  auto                   count   = freerdp_get_event_handles(client.instance->context, handles.data(), handles.size());
  ASSERT_GT(count, 0u);
  ASSERT_LT(WaitForMultipleObjects(count, handles.data(), FALSE, 10000), WAIT_OBJECT_0 + count);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_DISCONNECTED; });
  });
  EXPECT_NE(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  backend.reset();
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "")) << logs.Text(true);
  RecordProperty("trace", logs.Text(true));
}
}
