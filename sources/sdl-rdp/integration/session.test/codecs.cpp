#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waiting-open.hpp>
#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>

namespace sdl_rdp::integration::session_test::detail::codecs {
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::WaitingOpen;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::codec::Gate;
using sdl_rdp::headless_client_test::codec::Mode;
using sdl_rdp::headless_client_test::codec::ModeName;
using sdl_rdp::headless_client_test::codec::NegotiatedCodec;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::SystemCall;
using sdl_rdp::utilities::support_test::OutOfRangeEnum;

namespace {
// A flat colour survives every lossy codec within three levels per channel.
constexpr std::uint32_t FlatColourError = 3;
auto ThenUnblockedPresent(std::future<int>& presenting, Client& client) -> void {
  auto ready = presenting.wait_for(std::chrono::seconds(10));
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready != std::future_status::ready) client.Disconnect();
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
auto ThenDisplaced(Client& first) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(first.Instance()->context->input, KBD_FLAGS_DOWN, 0x30));
  auto deadline  = Clock::now() + std::chrono::seconds(10);
  bool connected = true;
  while (connected && Clock::now() < deadline) connected = first.Pump();
  ASSERT_FALSE(connected);
  EXPECT_EQ(freerdp_get_last_error(first.Instance()->context), FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION);
}
}
TEST_P(Gate, FramesAndInput) {
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  ASSERT_NO_FATAL_FAILURE(PresentMeasuredFrame(client));
  ASSERT_NO_FATAL_FAILURE(WhenDamagedBlock(client));
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ThenClientDisconnects(client);
}
TEST_P(Gate, ResizeAndWakeup) {
  ASSERT_NO_FATAL_FAILURE(Reopen(640, 480));
  auto&  handle = *backend;
  Client client(sdlrdp_port(&handle), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(ThenResizedConnection());
  EXPECT_EQ(sdlrdp_wait(&handle, 1), 0);
  auto waiter   = std::async(std::launch::async, [&handle] { return sdlrdp_wait(&handle, 30000); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (waiter.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && Clock::now() < deadline)
    sdlrdp_wakeup(&handle);
  EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_EQ(waiter.get(), 0);
  backend.Close();
}
TEST_P(Gate, LateClientAndBurst) {
  sdlrdp_rect const area{ 0, 0, 320, 200 };
  ASSERT_EQ(backend.Present(pixels, 320, 200, area), 0);
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  WhenBurstPictures(client, area);
}
TEST_P(Gate, DesktopIsPicture) {
  backend.Close();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 640, 480, 0, Logs::Collect, &logs };
  config.codec = GetParam().codec;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config));
  Pixels frame(640uz * 480);
  HashPattern(frame);
  sdlrdp_rect const area{ 0, 0, 640, 480 };
  ASSERT_EQ(backend.Present(frame, 640, 480, area), 0);
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_TRUE(client.Until([&] { return client.Matches(frame); })) << logs.Text();
  ThenPictureDesktop(client);
}
TEST_P(Gate, WaitForClient) {
  backend.Close();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 1 };
  config.codec = GetParam().codec;
  WaitingOpen const opening(config);
  auto              port    = opening.Receive(std::chrono::seconds(15)).value_or(0);
  ASSERT_GT(port, 0);
  EXPECT_FALSE(opening.Receive(std::chrono::milliseconds(0)).has_value());
  Client client(Narrowed<std::uint32_t>(port), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  EXPECT_EQ(opening.Receive(std::chrono::seconds(15)), std::optional(0));
}
TEST_P(Gate, BlockedSinglePresent) {
  ASSERT_NO_FATAL_FAILURE(Reopen(2048, 1536));
  Client client(sdlrdp_port(&*backend), GetParam().surface, 2048, 1536);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events.front().type, SDLRDP_CONNECTED);
  pixels.resize(2048uz * 1536);
  HashPattern(pixels);
  sdlrdp_rect area{ 0, 0, 2048, 1536 };
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async, [&] { return backend.Present(pixels, 2048, 1536, area); });
  ASSERT_NO_FATAL_FAILURE(ThenUnblockedPresent(presenting, client));
  EXPECT_TRUE(client.Until([&] { return client.Matches(pixels); }))
      << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver) {
  Client first(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_TRUE(first.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  Client second(sdlrdp_port(&*backend), GetParam().surface, 400, 240);
  ASSERT_TRUE(second.Connect()) << logs.Text(true);
  auto events = Events(3);
  ASSERT_NO_FATAL_FAILURE(ThenTakeoverEvents(events));
  ASSERT_NO_FATAL_FAILURE(ThenDisplaced(first));
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(second.Disconnect());
  ASSERT_NO_FATAL_FAILURE(ThenDisconnected());
  EXPECT_EQ(sdlrdp_wait(&*backend, 0), 0);
}
TEST_P(Gate, LiveCodecChange) {
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  auto previous = GetParam().codec;
  for (auto codec :
       { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_AUTO }) {
    ASSERT_EQ(sdlrdp_set_codec(&*backend, codec), 0);
    client.Tolerance(CodecTolerance(codec, GetParam().surface));
    std::ranges::fill(pixels, 0x00404040u + (Narrowed<std::uint32_t>(std::to_underlying(codec)) * 0x00040404u));
    ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
    auto expected = NegotiatedCodec(codec, GetParam().surface);
    ASSERT_NO_FATAL_FAILURE(ThenCodecChange(expected, previous));
    previous = expected;
  }
  auto const unlisted = OutOfRangeEnum<sdlrdp_codec>(99);
  EXPECT_EQ(sdlrdp_set_codec(&*backend, unlisted), -1);
}
TEST_P(Gate, ExactFlatColour) {
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  client.Tolerance(std::min(CodecTolerance(GetParam().codec, GetParam().surface), FlatColourError));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
}
TEST_P(Gate, TinyDamage) {
  Client client(sdlrdp_port(&*backend), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  pixels[(51 * 320) + 73] = 0x000000ff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 73, 51, 1, 1 }));
  pixels.back() = 0x00ffffff;
  Frame(client, { 319, 199, 1, 1 });
}
TEST_P(Gate, ProbeClosesBeforeActivation) {
  {
    Descriptor const socket { SystemCall(::socket(AF_INET, SOCK_STREAM, 0), "probe socket") };
    sockaddr_in      address{ };
    address.sin_family      = AF_INET;
    address.sin_port        = htons(sdlrdp_port(&*backend));
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
  EXPECT_EQ(sdlrdp_wait(&*backend, 0), 0);
}

INSTANTIATE_TEST_SUITE_P(Codec, Gate,
                         testing::Values(Mode{ true, SDLRDP_CODEC_RAW }, Mode{ true, SDLRDP_CODEC_PLANAR },
                                         Mode{ true, SDLRDP_CODEC_REMOTEFX }, Mode{ true, SDLRDP_CODEC_NSCODEC },
                                         Mode{ false, SDLRDP_CODEC_RAW }, Mode{ false, SDLRDP_CODEC_PLANAR }),
                         ModeName);
}
