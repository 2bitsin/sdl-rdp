#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/peer/peer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BackendGate {
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
  Client client(sdlrdp_port(backend.Handle()), true, 1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  FrameObserver              observer(client);
  std::vector<std::uint32_t> pixels(640uz * 480, 0x112233);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(sdlrdp_wait_frame(backend.Handle(), 10000), 1);
  observer.Clear(); // Test the negotiated window after ACK support is established.
  ASSERT_NO_FATAL_FAILURE(FillLegacyWindow(client, observer, pixels));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 3; }));
  EXPECT_TRUE(client.Matches(pixels));
  ASSERT_TRUE(observer.Ack());
  EXPECT_EQ(sdlrdp_wait_frame(backend.Handle(), 10000), 1);
  EXPECT_TRUE(observer.Coherent());
}
TEST_F(RoundFive, SuppressOutput) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client client(sdlrdp_port(backend.Handle()), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  FrameObserver observer(client);
  auto*         update   = client.Instance()->context->update;
  ASSERT_NO_FATAL_FAILURE(SuppressAndCheckInput(client));
  auto                       bytes  = client.Received();
  std::vector<std::uint32_t> pixels(640uz * 480, 0x123456);
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
  ASSERT_NO_FATAL_FAILURE(Open(640, 350, { 4, 3 }));
  Client client(sdlrdp_port(backend.Handle()), true, 1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  ASSERT_NO_FATAL_FAILURE(ThenAspectGeometry(client));
  FrameObserver              observer(client);
  std::vector<std::uint32_t> pixels(640uz * 350);
  std::fill_n(pixels.begin() + 175uz * 640, 640, 0xffffff);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 350));
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty(); }));
  ThenScaledHighlight(client);
  ASSERT_NO_FATAL_FAILURE(ThenAspectMouse(client));
  ASSERT_EQ(sdlrdp_set_aspect(backend.Handle(), { 0, 0 }), 0);
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->height == 350 && client.Matches(pixels); }));
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
}
TEST_F(RoundFive, SparseRegions) {
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC }) {
    ASSERT_NO_FATAL_FAILURE(Open(1024, 768, { }, codec));
    Client client(sdlrdp_port(backend.Handle()), true, 1024, 768);
    ASSERT_NO_FATAL_FAILURE(Connect(client, false));
    FrameObserver              observer(client);
    std::vector<std::uint32_t> pixels(1024uz * 768);
    Headless::HashPattern(pixels);
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 1024, 768));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
    auto bytes = client.Received();
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 1024, 768));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 2; }));
    auto bounding = client.Received() - bytes;
    RecordProperty("bounding_bytes_" + std::to_string(codec), std::to_string(bounding));
    ASSERT_NO_FATAL_FAILURE(ThenSparseDamage(client, observer, pixels, bounding, codec));
  }
}

namespace {
auto WaitForAcknowledgement(sdlrdp_handle& handle) -> bool {
  auto& frames = handle.Frames();
  auto  lock   = frames.Lock();
  Expects(handle.Session().Current(lock) != nullptr, "active peer owns the pending frame");
  return frames.WaitFor(lock, Backend::DeadlineAfter(std::chrono::seconds(10)),
                        [&] { return AllAcknowledged(handle, lock); });
}
}
TEST_F(RoundFive, WaitWithoutRefreshFeedback) {
  Expects(backend.Handle() == nullptr, "backend has not opened");
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  Client client(sdlrdp_port(backend.Handle()), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_EQ(Events(2).size(), 2u);
  FrameObserver                    observer(client);
  std::vector<std::uint32_t> const pixels(320uz * 200, 0x445566);
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
auto ProduceFrames(Headless::BackendInstance const& backend, std::atomic<std::size_t>& presents,
                   std::stop_token const& stop) -> void {
  std::vector<std::uint32_t> pixels(1024uz * 768);
  sdlrdp_rect const          area  { 0, 0, 1024, 768 };
  while (!stop.stop_requested()) {
    auto sequence = presents.load() + 1;
    std::fill_n(pixels.begin(), 1024, sequence);
    std::fill_n(pixels.end() - 1024, 1024, sequence);
    auto result = backend.Present(pixels, 1024, 768, area);
    Expects(result == 0, "concurrent present accepted");
    presents = sequence;
    std::this_thread::yield();
  }
}
}
TEST_F(RoundFive, ProducerDoesNotStarveOrTear) {
  ASSERT_NO_FATAL_FAILURE(Open(1024, 768));
  Client client(sdlrdp_port(backend.Handle()), true, 1024, 768);
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
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, SDLRDP_CODEC_AUTO));
  Client const client(sdlrdp_port(backend.Handle()), true);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ThenConnectedCodec(client, SDLRDP_CODEC_REMOTEFX);
}
namespace {
constexpr auto ExpectedTransportMessages = std::array<std::pair<char const*, char const*>, 9>{
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
}
namespace {
auto ThenDisconnectReason(Logs& logs, wLog* peer, char const* name) -> void {
  WLog_Print(peer, WLOG_ERROR, "%s [0x00010000]", name);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, name));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, name));
}
auto ThenTransportMessage(Logs& logs, char const* category, char const* message) -> void {
  WLog_Print(WLog_Get(category), WLOG_ERROR, "%s", message);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, message));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, message));
}
auto ThenExpectedTransportLogs(Logs& logs) -> void {
  auto* peer = WLog_Get("com.freerdp.core.peer");
  for (auto const* name :
       { "ERRINFO_LOGOFF_BY_USER", "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION", "ERRINFO_RPC_INITIATED_DISCONNECT" }) {
    ThenDisconnectReason(logs, peer, name);
  }
  for (auto [category, message] : ExpectedTransportMessages) {
    ThenTransportMessage(logs, category, message);
  }
}
}
TEST_F(RoundFive, ExpectedDisconnectLogLevels) {
  ASSERT_NO_FATAL_FAILURE(Open());
  ThenExpectedTransportLogs(logs);
  auto*       peer    = WLog_Get("com.freerdp.core.peer");
  auto const* failure = "BIO_write returned a system error 5: Input/output error";
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "%s", failure);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, failure));
  WLog_Print(peer, WLOG_ERROR, "%s", "BIO_read returned a system error 110: Connection timed out");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "BIO_read returned a system error 110"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "BIO_read returned a system error x5: bad errno");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "system error x5"));
  WLog_Print(peer, WLOG_ERROR, "transport failure marker");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "transport failure marker"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "ERRINFO_LOGOFF_BY_USER [0x0001000C]");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "ERRINFO_LOGOFF_BY_USER"));
  RecordProperty("trace", logs.Text(true));
}

TEST_F(RoundFive, GraphicsDisconnectDuringWrite) {
  constexpr std::uint32_t side = 2048;
  ASSERT_NO_FATAL_FAILURE(Open(side, side, { }, SDLRDP_CODEC_PROGRESSIVE));
  Client client(sdlrdp_port(backend.Handle()), true, side, side);
  ASSERT_NO_FATAL_FAILURE(ConnectPipeline(client));
  std::ignore = backend.Poll();
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, side, side));
  ASSERT_NO_FATAL_FAILURE(sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged(client, backend, logs));
  Headless::NoisePattern(pixels, 1);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, side, side));
  ASSERT_NO_FATAL_FAILURE(ThenReadable(client));
  ThenWriteDisconnect(client);
}
}
