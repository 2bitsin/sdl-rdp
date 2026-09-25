#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waiting-open.hpp>
#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>

namespace sdl_rdp::integration::session_test::detail::codecs {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::InvalidChoice;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::backend::Holds;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::backend::WaitingOpen;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::codec::Gate;
using sdl_rdp::headless_client_test::codec::Mode;
using sdl_rdp::headless_client_test::codec::ModeName;
using sdl_rdp::headless_client_test::codec::NegotiatedCodec;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Disconnected;
using sdl_rdp::link::Event;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::SystemCall;
using sdl_rdp::utilities::support_test::OutOfRangeEnum;

namespace {
// A flat colour survives every lossy codec within three levels per channel.
constexpr std::uint32_t FlatColourError = 3;
auto ThenUnblockedPresent(std::future<void>& presenting, Client& client) -> void {
  auto ready = presenting.wait_for(std::chrono::seconds(10));
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready != std::future_status::ready) client.Disconnect();
  presenting.get();
  ASSERT_EQ(ready, std::future_status::ready);
}
auto ThenTakeoverGeometry(Event const& event) -> void {
  auto const& connected = As<Connected>(event);
  EXPECT_EQ(connected.width, 320u);
  EXPECT_EQ(connected.height, 200u);
}
auto ThenTakeoverEvents(std::span<Event const> events) -> void {
  ASSERT_EQ(events.size(), 3u);
  EXPECT_TRUE(Holds<Disconnected>(events[0]));
  ThenTakeoverGeometry(events[1]);
  EXPECT_TRUE(Holds<ScreenChanged>(events[2]));
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
  Client client(backend.Port(), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  ASSERT_NO_FATAL_FAILURE(PresentMeasuredFrame(client));
  ASSERT_NO_FATAL_FAILURE(WhenDamagedBlock(client));
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ThenClientDisconnects(client);
}
TEST_P(Gate, ResizeAndWakeup) {
  ASSERT_NO_FATAL_FAILURE(Reopen(640, 480));
  Client client(backend.Port(), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(ThenResizedConnection());
  EXPECT_FALSE(backend.Wait(std::chrono::milliseconds{ 1 }));
  auto waiter   = std::async(std::launch::async, [&] { return backend.Wait(std::chrono::milliseconds{ 30000 }); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (waiter.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && Clock::now() < deadline)
    (*backend).Events().Wakeup();
  EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_FALSE(waiter.get());
  backend.Close();
}
TEST_P(Gate, LateClientAndBurst) {
  Rect const area{ .x = 0, .y = 0, .w = 320, .h = 200 };
  backend.Present(pixels, 320, 200, area);
  Client client(backend.Port(), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  WhenBurstPictures(client, area);
}
TEST_P(Gate, DesktopIsPicture) {
  backend.Close();
  auto config = LoopbackConfig(certificates.Path(), { .width = 640, .height = 480 });
  config.codec = GetParam().codec;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  Pixels frame(640uz * 480);
  HashPattern(frame);
  Rect const area{ .x = 0, .y = 0, .w = 640, .h = 480 };
  backend.Present(frame, 640, 480, area);
  Client client(backend.Port(), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_TRUE(client.Until([&] { return client.Matches(frame); })) << logs.Text();
  ThenPictureDesktop(client);
}
TEST_P(Gate, WaitForClient) {
  backend.Close();
  auto config = LoopbackConfig(certificates.Path());
  config.wait_for_client = true;
  config.codec           = GetParam().codec;
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
  Client client(backend.Port(), GetParam().surface, 2048, 1536);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_TRUE(Holds<Connected>(events.front()));
  pixels.resize(2048uz * 1536);
  HashPattern(pixels);
  Rect area{ .x = 0, .y = 0, .w = 2048, .h = 1536 };
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async, [&] { backend.Present(pixels, 2048, 1536, area); });
  ASSERT_NO_FATAL_FAILURE(ThenUnblockedPresent(presenting, client));
  EXPECT_TRUE(client.Until([&] { return client.Matches(pixels); }))
      << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver) {
  Client first(backend.Port(), GetParam().surface);
  ASSERT_TRUE(first.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  Client second(backend.Port(), GetParam().surface, 400, 240);
  ASSERT_TRUE(second.Connect()) << logs.Text(true);
  auto events = Events(3);
  ASSERT_NO_FATAL_FAILURE(ThenTakeoverEvents(events));
  ASSERT_NO_FATAL_FAILURE(ThenDisplaced(first));
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(second.Disconnect());
  ASSERT_NO_FATAL_FAILURE(ThenDisconnected());
  EXPECT_FALSE(backend.Wait(std::chrono::milliseconds{ 0 }));
}
TEST_P(Gate, LiveCodecChange) {
  Client client(backend.Port(), GetParam().surface);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  auto previous = GetParam().codec;
  for (auto codec : { Codec::Raw, Codec::Planar, Codec::RemoteFx, Codec::NsCodec, Codec::Auto }) {
    (*backend).Presentation().SetCodec(codec);
    client.Tolerance(CodecTolerance(codec, GetParam().surface));
    std::ranges::fill(pixels, 0x00404040u + (Narrowed<std::uint32_t>(std::to_underlying(codec)) * 0x00040404u));
    ASSERT_NO_FATAL_FAILURE(Frame(client, { .x = 0, .y = 0, .w = 320, .h = 200 }));
    auto expected = NegotiatedCodec(codec, GetParam().surface);
    ASSERT_NO_FATAL_FAILURE(ThenCodecChange(expected, previous));
    previous = expected;
  }
  auto const unlisted = OutOfRangeEnum<Codec>(99);
  EXPECT_THROW((*backend).Presentation().SetCodec(unlisted), InvalidChoice);
}
TEST_P(Gate, ExactFlatColour) {
  Client client(backend.Port(), GetParam().surface);
  client.Tolerance(std::min(CodecTolerance(GetParam().codec, GetParam().surface), FlatColourError));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame(client, { .x = 0, .y = 0, .w = 320, .h = 200 }));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
}
TEST_P(Gate, TinyDamage) {
  Client client(backend.Port(), GetParam().surface);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(client));
  ASSERT_NO_FATAL_FAILURE(Frame(client, { .x = 0, .y = 0, .w = 320, .h = 200 }));
  pixels[(51 * 320) + 73] = 0x000000ff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, { .x = 73, .y = 51, .w = 1, .h = 1 }));
  pixels.back() = 0x00ffffff;
  Frame(client, { .x = 319, .y = 199, .w = 1, .h = 1 });
}
TEST_P(Gate, ProbeClosesBeforeActivation) {
  {
    Descriptor const socket { SystemCall(::socket(AF_INET, SOCK_STREAM, 0), "probe socket") };
    sockaddr_in      address{ };
    address.sin_family      = AF_INET;
    address.sin_port        = htons(backend.Port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(connect(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
  }
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!logs.Contains(LogLevel::Info, "Connection closed before activation") && Clock::now() < deadline)
    std::this_thread::yield();
  {
    EXPECT_TRUE(std::ranges::any_of(logs.Entries(), [](auto const& line) {
      return line.first == LogLevel::Info
             && line.second == "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
    }));
  }
  EXPECT_FALSE(logs.Contains(LogLevel::Error, "Peer transport failed")) << logs.Text();
  EXPECT_FALSE(backend.Wait(std::chrono::milliseconds{ 0 }));
}

INSTANTIATE_TEST_SUITE_P(
    Codec, Gate,
    testing::Values(Mode{ .surface = true, .codec = Codec::Raw }, Mode{ .surface = true, .codec = Codec::Planar },
                    Mode{ .surface = true, .codec = Codec::RemoteFx }, Mode{ .surface = true, .codec = Codec::NsCodec },
                    Mode{ .surface = false, .codec = Codec::Raw }, Mode{ .surface = false, .codec = Codec::Planar }),
    ModeName);
}
