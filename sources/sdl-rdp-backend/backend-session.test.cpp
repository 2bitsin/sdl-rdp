#include <sdl-rdp/headless-client.test/gate.hpp>
#include <sdl-rdp/headless-client.test/mode.hpp>
#include <sdl-rdp/headless-client.test/waiting-open.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <algorithm>
#include <cstddef>
#include <future>

namespace BackendGate {
namespace {
auto ThenUnblockedPresent(std::future<int>& presenting, Client const& client) -> void {
  auto ready = presenting.wait_for(std::chrono::seconds(10));
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready != std::future_status::ready) freerdp_disconnect(client.Instance().get());
  ASSERT_EQ(presenting.get(), 0);
  ASSERT_EQ(ready, std::future_status::ready);
}
auto ThenTakeoverGeometry(sdlrdp_event const& event) -> void {
  EXPECT_EQ(event.connected.width, 320u);
  EXPECT_EQ(event.connected.height, 200u);
}
auto ThenTakeoverEvents(std::span<sdlrdp_event const> events) -> void {
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(events[1].type, SDLRDP_CONNECTED);
  ThenTakeoverGeometry(events[1]);
  EXPECT_EQ(events[2].type, SDLRDP_SCREEN);
}
auto ThenDisplaced(Client const& first) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(first.Instance()->context->input, KBD_FLAGS_DOWN, 0x30));
  auto deadline  = Clock::now() + std::chrono::seconds(10);
  bool connected = true;
  while (connected && Clock::now() < deadline) connected = first.Pump();
  ASSERT_FALSE(connected);
  EXPECT_EQ(freerdp_get_last_error(first.Instance()->context), FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION);
}
auto CodecTolerance(sdlrdp_codec codec, bool surface) -> std::uint32_t {
  switch (codec) {
  case SDLRDP_CODEC_REMOTEFX:
  case SDLRDP_CODEC_AUTO: return surface ? 40 : 0;
  default:                return 0;
  }
}
auto NegotiatedCodec(sdlrdp_codec requested, bool surface) -> sdlrdp_codec {
  switch (requested) {
  case SDLRDP_CODEC_AUTO: return surface ? SDLRDP_CODEC_REMOTEFX : SDLRDP_CODEC_PLANAR;
  case SDLRDP_CODEC_REMOTEFX:
  case SDLRDP_CODEC_NSCODEC: return surface ? requested : SDLRDP_CODEC_PLANAR;
  case SDLRDP_CODEC_PLANAR:
  case SDLRDP_CODEC_RAW: return requested;
  default:               utilities::Unreachable(requested);
  }
}
}
TEST_P(Gate, FramesAndInput) {
  Client client(sdlrdp_port(backend.get()), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenConnected();
  if (::testing::Test::HasFatalFailure()) return;
  PresentMeasuredFrame(client);
  if (::testing::Test::HasFatalFailure()) return;
  WhenDamagedBlock(client);
  if (::testing::Test::HasFatalFailure()) return;
  Input(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenClientDisconnects(client);
}
TEST_P(Gate, ResizeAndWakeup) {
  Reopen(640, 480);
  if (::testing::Test::HasFatalFailure()) return;
  auto*  handle = backend.get();
  Client client(sdlrdp_port(handle), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenResizedConnection();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_wait(handle, 1), 0);
  auto waiter   = std::async(std::launch::async, [=] { return sdlrdp_wait(handle, 30000); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (waiter.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && Clock::now() < deadline)
    sdlrdp_wakeup(handle);
  EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_EQ(waiter.get(), 0);
  backend.reset();
}
TEST_P(Gate, LateClientAndBurst) {
  sdlrdp_rect const area{ 0, 0, 320, 200 };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  Client client(sdlrdp_port(backend.get()), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  WhenBurstPictures(client, area);
}
TEST_P(Gate, DesktopIsPicture) {
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 640, 480, 0, Logs::Collect, &logs };
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  std::vector<UINT32> frame(640uz * 480);
  std::ranges::generate(frame, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect const area{ 0, 0, 640, 480 };
  ASSERT_EQ(sdlrdp_present(handle, frame.data(), 640 * 4, 640, 480, &area, 1), 0);
  Client client(sdlrdp_port(handle), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return client.Matches(frame); })) << logs.Text();
  ThenPictureDesktop(client);
}
TEST_P(Gate, WaitForClient) {
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 1 };
  config.codec = GetParam().codec;
  WaitingOpen const opening(config);
  auto              port    = opening.Receive(std::chrono::seconds(15)).value_or(0);
  ASSERT_GT(port, 0);
  EXPECT_FALSE(opening.Receive(std::chrono::milliseconds(0)).has_value());
  Client client(unsigned(port), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(opening.Receive(std::chrono::seconds(15)), std::optional(0));
}
TEST_P(Gate, BlockedSinglePresent) {
  Reopen(2048, 1536);
  if (::testing::Test::HasFatalFailure()) return;
  auto*  handle = backend.get();
  Client client(sdlrdp_port(handle), GetParam().surface, 2048, 1536);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events.front().type, SDLRDP_CONNECTED);
  pixels.resize(2048uz * 1536);
  std::ranges::generate(pixels, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect area{ 0, 0, 2048, 1536 };
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async,
                               [&] { return sdlrdp_present(handle, pixels.data(), 2048 * 4, 2048, 1536, &area, 1); });
  ThenUnblockedPresent(presenting, client);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_TRUE(client.Until([&] { return client.Matches(pixels); }))
      << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver) {
  Client const first(sdlrdp_port(backend.get()), GetParam().surface);
  ASSERT_TRUE(freerdp_connect(first.Instance().get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  Client second(sdlrdp_port(backend.get()), GetParam().surface, 400, 240);
  ASSERT_TRUE(freerdp_connect(second.Instance().get())) << logs.Text(true);
  auto events = Events(3);
  ThenTakeoverEvents(events);
  if (::testing::Test::HasFatalFailure()) return;
  ThenDisplaced(first);
  if (::testing::Test::HasFatalFailure()) return;
  Input(second);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(freerdp_disconnect(second.Instance().get()));
  ThenDisconnected();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}
TEST_P(Gate, LiveCodecChange) {
  Client client(sdlrdp_port(backend.get()), GetParam().surface);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  auto previous = GetParam().codec;
  for (auto codec :
       { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_AUTO }) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), codec), 0);
    client.Tolerance(CodecTolerance(codec, GetParam().surface));
    std::ranges::fill(pixels, 0x00404040u + (unsigned(codec) * 0x00040404u));
    Frame(client, { 0, 0, 320, 200 });
    if (::testing::Test::HasFatalFailure()) return;
    auto expected = NegotiatedCodec(codec, GetParam().surface);
    ThenCodecChange(expected, previous);
    if (::testing::Test::HasFatalFailure()) return;
    previous = expected;
  }
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): Invalid codec ABI test.
  EXPECT_EQ(sdlrdp_set_codec(backend.get(), sdlrdp_codec(99)), -1);
}
TEST_P(Gate, ExactFlatColour) {
  Client client(sdlrdp_port(backend.get()), GetParam().surface);
  client.Tolerance(GetParam().codec == SDLRDP_CODEC_REMOTEFX || GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  std::ranges::fill(pixels, 0x00ffffffu);
  Frame(client, { 0, 0, 320, 200 });
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
}
TEST_P(Gate, TinyDamage) {
  Client client(sdlrdp_port(backend.get()), GetParam().surface);
  ConnectCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  Frame(client, { 0, 0, 320, 200 });
  if (::testing::Test::HasFatalFailure()) return;
  pixels[(51 * 320) + 73] = 0x000000ff;
  Frame(client, { 73, 51, 1, 1 });
  if (::testing::Test::HasFatalFailure()) return;
  pixels.back() = 0x00ffffff;
  Frame(client, { 319, 199, 1, 1 });
}
TEST_P(Gate, ProbeClosesBeforeActivation) {
  {
    Backend::Descriptor const socket { Backend::SystemCall(::socket(AF_INET, SOCK_STREAM, 0), "probe socket") };
    sockaddr_in               address{ };
    address.sin_family      = AF_INET;
    address.sin_port        = htons(sdlrdp_port(backend.get()));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(connect(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
  }
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!logs.Contains(SDLRDP_LOG_INFO, "Connection closed before activation") && Clock::now() < deadline)
    std::this_thread::yield();
  {
    EXPECT_TRUE(std::ranges::any_of(logs.Entries(), [](auto const& line) {
      return line.first == SDLRDP_LOG_INFO
             && line.second == "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
    }));
  }
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}

INSTANTIATE_TEST_SUITE_P(Codec, Gate,
                         testing::Values(Mode{ true, SDLRDP_CODEC_RAW }, Mode{ true, SDLRDP_CODEC_PLANAR },
                                         Mode{ true, SDLRDP_CODEC_REMOTEFX }, Mode{ true, SDLRDP_CODEC_NSCODEC },
                                         Mode{ false, SDLRDP_CODEC_RAW }, Mode{ false, SDLRDP_CODEC_PLANAR }),
                         ModeName);
}
