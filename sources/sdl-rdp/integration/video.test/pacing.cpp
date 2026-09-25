#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/aspect-ratio.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace sdl_rdp::integration::video_test::detail::pacing {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::AllAcknowledged;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::headless_client_test::frame::NoisePattern;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::session::Backend;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Rect;

namespace {
auto ThenSuppressed(Client& client, FrameObserver const& observer, std::uint64_t bytes) -> void {
  // Probe for forbidden output after the ordered suppression barrier. No
  // required event or minimum amount of work depends on this observation span.
  for (std::size_t i = 0; i < 10; ++i) ASSERT_TRUE(client.Pump(5));
  EXPECT_EQ(client.Received(), bytes);
  EXPECT_TRUE(observer.Frames().empty());
}
}
TEST_F(RoundFive, DelayedAcknowledgements) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client client(backend.Port(), true, 1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  FrameObserver observer(client);
  Pixels        pixels(640uz * 480, 0x112233);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 10000 }));
  observer.Clear(); // Test the negotiated window after ACK support is established.
  ASSERT_NO_FATAL_FAILURE(FillLegacyWindow(client, observer, pixels));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 3; }));
  EXPECT_TRUE(client.Matches(pixels));
  ASSERT_TRUE(observer.Ack());
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 10000 }));
  EXPECT_TRUE(observer.Coherent());
}
TEST_F(RoundFive, SuppressOutput) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client client(backend.Port(), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  FrameObserver observer(client);
  auto*         update   = client.Instance()->context->update;
  ASSERT_NO_FATAL_FAILURE(SuppressAndCheckInput(client));
  auto   bytes  = client.Received();
  Pixels pixels(640uz * 480, 0x123456);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  std::ranges::fill(pixels, 0x654321);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_NO_FATAL_FAILURE(ThenSuppressed(client, observer, bytes));
  RECTANGLE_16 const area{ 0, 0, 639, 479 };
  ASSERT_TRUE(update->SuppressOutput(client.Instance()->context, 1, &area));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  EXPECT_TRUE(client.Matches(pixels));
}
TEST_F(RoundFive, AspectAndMouse) {
  ASSERT_NO_FATAL_FAILURE(Open(640, 350, AspectRatio{ .numerator = 4, .denominator = 3 }));
  Client client(backend.Port(), true, 1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  ASSERT_NO_FATAL_FAILURE(ThenAspectGeometry(client));
  FrameObserver observer(client);
  Pixels        pixels(640uz * 350);
  std::fill_n(pixels.begin() + 175uz * 640, 640, 0xffffff);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 350));
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty(); }));
  ThenScaledHighlight(client);
  ASSERT_NO_FATAL_FAILURE(ThenAspectMouse(client));
  (*backend).Presentation().SetAspect(std::nullopt);
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->height == 350 && client.Matches(pixels); }));
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
}
TEST_F(RoundFive, SparseRegions) {
  for (auto codec : { Codec::Raw, Codec::Planar, Codec::RemoteFx, Codec::NsCodec }) {
    ASSERT_NO_FATAL_FAILURE(Open(1024, 768, { }, codec));
    Client client(backend.Port(), true, 1024, 768);
    ASSERT_NO_FATAL_FAILURE(Connect(client, false));
    FrameObserver observer(client);
    Pixels        pixels(1024uz * 768);
    HashPattern(pixels);
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 1024, 768));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
    auto bytes = client.Received();
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 1024, 768));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 2; }));
    auto bounding = client.Received() - bytes;
    RecordProperty("bounding_bytes_" + std::to_string(std::to_underlying(codec)), std::to_string(bounding));
    ASSERT_NO_FATAL_FAILURE(ThenSparseDamage(client, observer, pixels, bounding, codec));
  }
}

namespace {
auto WaitForAcknowledgement(Backend& backend) -> bool {
  auto&      frames  = backend.Frames();
  auto       lock    = frames.Lock();
  auto const current = backend.Session().Current(lock);
  Expects(current.has_value(), "active peer owns the pending frame");
  return frames.WaitFor(lock, DeadlineAfter(std::chrono::seconds(10)), [&] { return AllAcknowledged(backend, lock); });
}
}
TEST_F(RoundFive, WaitWithoutRefreshFeedback) {
  Expects(!backend, "backend has not opened");
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  Client client(backend.Port(), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_EQ(Events(2).size(), 2u);
  FrameObserver observer(client);
  Pixels const  pixels(320uz * 200, 0x445566);
  for (std::size_t i = 1; i <= 20; ++i) {
    ASSERT_NO_FATAL_FAILURE(WhenAcknowledgedFrame(client, observer, pixels, i, WaitForAcknowledgement));
  }
}
TEST_F(RoundFive, NeverAcknowledges) {
  ThenNeverAcknowledges([](auto run) { run(); });
}
TEST_F(RoundFive, ColourDepths) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  for (auto depth : { 16u, 24u }) {
    ASSERT_NO_FATAL_FAILURE(ThenColourDepth(depth));
  }
}

namespace {
auto ProduceFrames(BackendInstance const& backend, std::atomic<std::size_t>& presents, std::stop_token const& stop)
    -> void {
  Pixels     pixels(1024uz * 768);
  Rect const area   { .x = 0, .y = 0, .w = 1024, .h = 768 };
  auto const failed = [](std::string_view text) { ADD_FAILURE() << "the producer's present failed: " << text; };
  while (!stop.stop_requested()) {
    auto sequence = presents.load() + 1;
    std::fill_n(pixels.begin(), 1024, sequence);
    std::fill_n(pixels.end() - 1024, 1024, sequence);
    if (!Contained([&] { backend.Present(pixels, 1024, 768, area); }, failed)) return;
    presents = sequence;
    std::this_thread::yield();
  }
}
}
TEST_F(RoundFive, ProducerDoesNotStarveOrTear) {
  ASSERT_NO_FATAL_FAILURE(Open(1024, 768));
  Client client(backend.Port(), true, 1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  FrameObserver            observer(client);
  std::atomic<std::size_t> presents = 0;
  std::jthread             producer([&](std::stop_token const& stop) { ProduceFrames(backend, presents, stop); });
  for (std::size_t i = 0; i < 20; ++i) {
    auto before       = presents.load();
    auto acknowledged = observer.Frames().size();
    ASSERT_TRUE(client.Until([&] { return presents.load() > before && observer.Frames().size() > acknowledged; }))
        << "both producer and consumer must progress";
    ASSERT_TRUE(observer.Ack());
  }
  producer.request_stop();
  producer.join();
  ThenProducerFrame(client, observer, presents);
}

TEST_F(RoundFive, AutoPrefersRemoteFX) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, Codec::Auto));
  Client client(backend.Port(), true);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ThenConnectedCodec(client, Codec::RemoteFx);
}
namespace {
constexpr auto ExpectedTransportMessages = std::array<std::pair<std::string_view, std::string_view>, 9>{
  { { "com.freerdp.core.peer"     , "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]"               },
    { "com.freerdp.core.transport", "BIO_read retries exceeded"                                      },
    { "com.freerdp.core.transport", "BIO_read returned a system error 104: Connection reset by peer" },
    { "com.freerdp.core.transport",
      "BIO_should_retry returned an error: error:80000068:system library::Connection reset by peer" },
    { "com.freerdp.core.transport", "BIO_write returned a system error 32: Broken pipe"                              },
    { "com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000020:system library::Broken pipe" },
    { "com.freerdp.core.transport", "BIO_read returned a system error 110: Connection timed out"                     },
    { "com.freerdp.core.transport", "BIO_read returned a system error 5: Input/output error"                         },
    { "com.freerdp.core"          , "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]"                               } }
};
auto ThenDisconnectReason(Logs& logs, wLog& peer, std::string const& name) -> void {
  WLog_Print(&peer, WLOG_ERROR, "%s [0x00010000]", name.c_str());
  EXPECT_TRUE(logs.Contains(LogLevel::Info, name));
  EXPECT_FALSE(logs.Contains(LogLevel::Error, name));
}
auto ThenTransportMessage(Logs& logs, std::string const& category, std::string const& message) -> void {
  WLog_Print(WLog_Get(category.c_str()), WLOG_ERROR, "%s", message.c_str());
  EXPECT_TRUE(logs.Contains(LogLevel::Info, message));
  EXPECT_FALSE(logs.Contains(LogLevel::Error, message));
}
auto ThenExpectedTransportLogs(Logs& logs) -> void {
  auto& peer = *WLog_Get("com.freerdp.core.peer");
  for (std::string const name :
       { "ERRINFO_LOGOFF_BY_USER", "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION", "ERRINFO_RPC_INITIATED_DISCONNECT" }) {
    ThenDisconnectReason(logs, peer, name);
  }
  for (auto [category, message] : ExpectedTransportMessages) {
    ThenTransportMessage(logs, std::string{ category }, std::string{ message });
  }
}
}
TEST_F(RoundFive, ExpectedDisconnectLogLevels) {
  ASSERT_NO_FATAL_FAILURE(Open());
  ThenExpectedTransportLogs(logs);
  auto*       peer    = WLog_Get("com.freerdp.core.peer");
  auto const* failure = "BIO_write returned a system error 5: Input/output error";
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "%s", failure);
  EXPECT_TRUE(logs.Contains(LogLevel::Error, failure));
  WLog_Print(peer, WLOG_ERROR, "%s", "BIO_read returned a system error 110: Connection timed out");
  EXPECT_TRUE(logs.Contains(LogLevel::Error, "BIO_read returned a system error 110"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "BIO_read returned a system error x5: bad errno");
  EXPECT_TRUE(logs.Contains(LogLevel::Error, "system error x5"));
  WLog_Print(peer, WLOG_ERROR, "transport failure marker");
  EXPECT_TRUE(logs.Contains(LogLevel::Error, "transport failure marker"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "ERRINFO_LOGOFF_BY_USER [0x0001000C]");
  EXPECT_TRUE(logs.Contains(LogLevel::Error, "ERRINFO_LOGOFF_BY_USER"));
  RecordProperty("trace", logs.Text(true));
}

TEST_F(RoundFive, GraphicsDisconnectDuringWrite) {
  constexpr std::uint32_t side = 2048;
  ASSERT_NO_FATAL_FAILURE(Open(side, side, { }, Codec::Progressive));
  Client client(backend.Port(), true, side, side);
  ASSERT_NO_FATAL_FAILURE(ConnectPipeline(client));
  std::ignore = backend.Poll();
  Pixels pixels(static_cast<std::size_t>(side) * side);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, side, side));
  ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(client, backend, logs));
  NoisePattern(pixels, 1);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, side, side));
  ASSERT_NO_FATAL_FAILURE(ThenReadable(client));
  ThenWriteDisconnect(client);
}
}
