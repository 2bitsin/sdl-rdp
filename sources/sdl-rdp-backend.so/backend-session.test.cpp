#include <cstddef>
#include <algorithm>

#include "_detail/test-backend.hpp"

namespace BackendGate {
TEST_P(Gate, FramesAndInput)
{
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 320u);
  EXPECT_EQ(events[0].connected.height, 200u);
  EXPECT_EQ(events[0].connected.bpp, 32u);
  EXPECT_EQ(events[0].connected.codec, GetParam().codec);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  FrameCounter const counter(client);
  auto bytes = client.Received();
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  if (GetParam().codec == SDLRDP_CODEC_PLANAR)
    EXPECT_LE(counter.bitmap_pdus, 1 + (client.Received() - bytes) / 0xFFFF);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
  RecordProperty("codec", std::to_string(GetParam().codec));
  RecordProperty("surface", bool(GetParam()) ? "true" : "false");
  for (unsigned y = 51; y < 81; ++y) std::fill_n(pixels.begin() + static_cast<std::size_t>(y) * 320 + 73, 40, 0x00020202);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 73, 51, 40, 30 }));
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  backend.reset();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "disconnected"));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
}
TEST_P(Gate, ResizeAndWakeup)
{
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0 };
  config.codec          = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 640u);
  EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[1].screen.width, 320u);
  EXPECT_EQ(events[1].screen.height, 200u);
  EXPECT_EQ(sdlrdp_wait(handle, 1), 0);
  auto waiter   = std::async(std::launch::async, [=] { return sdlrdp_wait(handle, 30000); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (waiter.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && Clock::now() < deadline) sdlrdp_wakeup(handle);
  EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_EQ(waiter.get(), 0);
  backend.reset();
}
TEST_P(Gate, LateClientAndBurst)
{
  sdlrdp_rect const area{ 0, 0, 320, 200 };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  auto before = ResidentBytes();
  for (unsigned frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(std::int64_t(after) - std::int64_t(before)));
  EXPECT_LE(after, before + pixels.size() * 16);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
TEST_P(Gate, DesktopIsPicture)
{
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0, Logs::Collect, &logs };
  config.codec          = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  std::vector<UINT32> frame(640uz * 480);
  std::ranges::generate(frame, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect const area{ 0, 0, 640, 480 };
  ASSERT_EQ(sdlrdp_present(handle, frame.data(), 640 * 4, 640, 480, &area, 1), 0);
  Client client(sdlrdp_port(handle), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return client.Matches(frame); })) << logs.Text();
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  EXPECT_EQ(client.instance->context->gdi->height, 480);
  EXPECT_FALSE(logs.Contains("failed"));
}
TEST_P(Gate, WaitForClient)
{
  auto port = sdlrdp_port(backend.get());
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", port, certificates.path.c_str(), 320, 200, 1 };
  config.codec            = GetParam().codec;
  sdlrdp_handle* handle   = nullptr;
  auto           opening  = std::async(std::launch::async, [&] { return sdlrdp_open(&config, &handle); });
  auto           deadline = Clock::now() + std::chrono::seconds(10);
  while (!Listening(port) && Clock::now() < deadline) std::this_thread::yield();
  EXPECT_EQ(opening.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  Client client(port, GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(opening.wait_for(std::chrono::seconds(10)), std::future_status::ready);
  ASSERT_EQ(opening.get(), 0);
  backend.reset(handle);
  EXPECT_EQ(sdlrdp_wait(handle, 0), 1);
  sdlrdp_event event{ };
  ASSERT_EQ(sdlrdp_poll(handle, &event, 1), 1u);
  EXPECT_EQ(event.type, SDLRDP_CONNECTED);
}
TEST_P(Gate, BlockedSinglePresent)
{
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.path.c_str(), 2048, 1536, 0 };
  config.codec          = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam(), 2048, 1536);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events.front().type, SDLRDP_CONNECTED);
  pixels.resize(2048uz * 1536);
  std::ranges::generate(pixels, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect area{ 0, 0, 2048, 1536 };
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async, [&] {
    return sdlrdp_present(handle, pixels.data(), 2048 * 4, 2048, 1536, &area, 1);
  });
  auto ready = presenting.wait_for(std::chrono::seconds(10));
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready != std::future_status::ready) freerdp_disconnect(client.instance.get());
  ASSERT_EQ(presenting.get(), 0);
  ASSERT_EQ(ready, std::future_status::ready);
  EXPECT_TRUE(client.Until([&] { return client.Matches(pixels); }))
      << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver)
{
  Client first(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(first.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  Client second(sdlrdp_port(backend.get()), GetParam(), 400, 240);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << logs.Text(true);
  auto events = Events(3);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(events[1].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[1].connected.width, 320u);
  EXPECT_EQ(events[1].connected.height, 200u);
  EXPECT_EQ(events[2].type, SDLRDP_SCREEN);
  freerdp_input_send_keyboard_event(first.instance->context->input, KBD_FLAGS_DOWN, 0x30);
  auto deadline  = Clock::now() + std::chrono::seconds(10);
  bool connected = true;
  while (connected && Clock::now() < deadline) connected = first.Pump();
  ASSERT_FALSE(connected);
  EXPECT_EQ(freerdp_get_last_error(first.instance->context), FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION);
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(freerdp_disconnect(second.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}
TEST_P(Gate, LiveCodecChange)
{
  Client client(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  auto previous = GetParam().codec;
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX,
                      SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_AUTO }) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), codec), 0);
    client.tolerance = (codec == SDLRDP_CODEC_REMOTEFX || codec == SDLRDP_CODEC_AUTO) && GetParam().surface ? 40 : 0;
    std::ranges::fill(pixels, 0x00404040u + (unsigned(codec) * 0x00040404u));
    ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
    auto expected = (!GetParam().surface && (codec == SDLRDP_CODEC_REMOTEFX || codec == SDLRDP_CODEC_NSCODEC)) ? SDLRDP_CODEC_PLANAR
                    : codec == SDLRDP_CODEC_AUTO                                                               ? (GetParam().surface ? SDLRDP_CODEC_REMOTEFX : SDLRDP_CODEC_PLANAR)
                                                                                                               : codec;

    if (expected != previous) {
      auto events = EventsUntil([](auto const& events) {
        return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CODEC_CHANGED; });
      },
                                false);
      ASSERT_EQ(events.size(), 1u);
      EXPECT_EQ(events[0].type, SDLRDP_CODEC_CHANGED);
      EXPECT_EQ(events[0].codec_changed.codec, expected);
    } else {
      std::array<sdlrdp_event, 4> events{ };
      auto                        count  = sdlrdp_poll(backend.get(), events.data(), events.size());
      EXPECT_TRUE(std::ranges::all_of(std::span(events).first(count),
                                      [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
    }
    previous = expected;
  }
  EXPECT_EQ(sdlrdp_set_codec(backend.get(), sdlrdp_codec(99)), -1);
}
TEST_P(Gate, ExactFlatColour)
{
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX || GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
}
TEST_P(Gate, TinyDamage)
{
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                                               : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  pixels[(51 * 320) + 73] = 0x000000ff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 73, 51, 1, 1 }));
  pixels.back() = 0x00ffffff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 319, 199, 1, 1 }));
}
TEST_P(Gate, ProbeClosesBeforeActivation)
{
  {
    Socket const socket;
    sockaddr_in  address{ };
    address.sin_family      = AF_INET;
    address.sin_port        = htons(sdlrdp_port(backend.get()));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(connect(socket.descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
  }
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!logs.Contains(SDLRDP_LOG_INFO, "Connection closed before activation") && Clock::now() < deadline) std::this_thread::yield();
  {
    std::scoped_lock const lock(logs.guard);
    EXPECT_TRUE(std::ranges::any_of(logs.lines, [](auto const& line) {
      return line.first == SDLRDP_LOG_INFO && line.second == "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
    }));
  }
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}

INSTANTIATE_TEST_SUITE_P(Codec, Gate, testing::Values(Mode{ true, SDLRDP_CODEC_RAW }, Mode{ true, SDLRDP_CODEC_PLANAR }, Mode{ true, SDLRDP_CODEC_REMOTEFX }, Mode{ true, SDLRDP_CODEC_NSCODEC }, Mode{ false, SDLRDP_CODEC_RAW }, Mode{ false, SDLRDP_CODEC_PLANAR }), ModeName);
}
