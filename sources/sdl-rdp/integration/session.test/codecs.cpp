#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waiting-open.hpp>
#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/descriptor.posix.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <future>
#include <netinet/in.h>
#include <sys/socket.h>
#include <utility>

namespace sdl_rdp::integration::session_test::detail::codecs {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::InvalidChoice;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::Holds;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::backend::WaitingOpen;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::codec::Gate;
using sdl_rdp::headless_client_test::codec::Mode;
using sdl_rdp::headless_client_test::codec::ModeName;
using sdl_rdp::headless_client_test::codec::NegotiatedCodec;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Disconnected;
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
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  ASSERT_NO_FATAL_FAILURE(PresentMeasuredFrame());
  ASSERT_NO_FATAL_FAILURE(WhenDamagedBlock());
  ASSERT_NO_FATAL_FAILURE(Input(*client));
  ThenClientDisconnects();
}
TEST_P(Gate, ResizeAndWakeup) {
  ASSERT_NO_FATAL_FAILURE(Reopen(640, 480));
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
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
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_TRUE(UntilMatches(*client, pixels)) << logs.Text();
  WhenBurstPictures(area);
}
TEST_P(Gate, DesktopIsPicture) {
  ASSERT_NO_FATAL_FAILURE(Reopen(640, 480));
  Pixels frame(640uz * 480);
  HashPattern(frame);
  Rect const area{ .x = 0, .y = 0, .w = 640, .h = 480 };
  backend.Present(frame, 640, 480, area);
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_TRUE(UntilMatches(*client, frame)) << logs.Text();
  ThenPictureDesktop();
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
  Client waiting(Narrowed<std::uint32_t>(port), GetParam().surface);
  ASSERT_TRUE(waiting.Connect()) << logs.Text(true);
  EXPECT_EQ(opening.Receive(std::chrono::seconds(15)), std::optional(0));
}
TEST_P(Gate, BlockedSinglePresent) {
  ASSERT_NO_FATAL_FAILURE(Reopen(2048, 1536));
  ASSERT_NO_FATAL_FAILURE(ConnectCodec(2048, 1536));
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_TRUE(Holds<Connected>(events.front()));
  pixels.resize(2048uz * 1536);
  HashPattern(pixels);
  Rect area{ .x = 0, .y = 0, .w = 2048, .h = 1536 };
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async, [&] { backend.Present(pixels, 2048, 1536, area); });
  ASSERT_NO_FATAL_FAILURE(ThenUnblockedPresent(presenting, *client));
  EXPECT_TRUE(UntilMatches(*client, pixels)) << "maximum channel error " << client->MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client->MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver) {
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  Client second(backend.Port(), GetParam().surface, 400, 240);
  ASSERT_TRUE(second.Connect()) << logs.Text(true);
  auto events = Events(3);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_TRUE(Holds<Disconnected>(events[0]));
  ThenConnectionDetails(events[1]);
  EXPECT_TRUE(Holds<ScreenChanged>(events[2]));
  ASSERT_NO_FATAL_FAILURE(ThenDisplaced(*client));
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(second.Disconnect());
  ASSERT_NO_FATAL_FAILURE(ThenDisconnected());
  EXPECT_FALSE(backend.Wait(std::chrono::milliseconds{ 0 }));
}
TEST_P(Gate, LiveCodecChange) {
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  auto previous = GetParam().codec;
  for (auto codec : { Codec::Raw, Codec::Planar, Codec::RemoteFx, Codec::NsCodec, Codec::Auto }) {
    (*backend).Presentation().SetCodec(codec);
    client->Tolerance(CodecTolerance(codec, GetParam().surface));
    std::ranges::fill(pixels, 0x00404040u + (Narrowed<std::uint32_t>(std::to_underlying(codec)) * 0x00040404u));
    ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
    auto expected = NegotiatedCodec(codec, GetParam().surface);
    ASSERT_NO_FATAL_FAILURE(ThenCodecChange(expected, previous));
    previous = expected;
  }
  auto const unlisted = OutOfRangeEnum<Codec>(99);
  EXPECT_THROW((*backend).Presentation().SetCodec(unlisted), InvalidChoice);
}
TEST_P(Gate, ExactFlatColour) {
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_NO_FATAL_FAILURE(ThenConnected());
  client->Tolerance(std::min(client->Tolerance(), FlatColourError));
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  RecordProperty("maximum_channel_error", client->MaxError(pixels));
}
TEST_P(Gate, TinyDamage) {
  ASSERT_NO_FATAL_FAILURE(ConnectCodec());
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  pixels[(51 * 320) + 73] = 0x000000ff;
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 73, .y = 51, .w = 1, .h = 1 }));
  pixels.back() = 0x00ffffff;
  Frame({ .x = 319, .y = 199, .w = 1, .h = 1 });
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
