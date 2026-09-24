#include "_detail/test-backend.hpp"

#include <algorithm>
#include <cstddef>

namespace BackendGate {
namespace {
void ThenSuppressed(Client& client, FrameObserver const& observer, uint64_t bytes) {
  // Probe for forbidden output after the ordered suppression barrier. No
  // required event or minimum amount of work depends on this observation span.
  for (unsigned i = 0; i < 10; ++i)
    ASSERT_TRUE(client.Pump(5));
  EXPECT_EQ(client.Received(), bytes);
  EXPECT_TRUE(observer.Frames().empty());
}
}
TEST_F(RoundFive, DelayedAcknowledgements) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver       observer(client);
  std::vector<UINT32> pixels(640uz * 480, 0x112233);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  observer.Clear(); // Test the negotiated window after ACK support is established.
  FillLegacyWindow(client, observer, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 3; }));
  EXPECT_TRUE(client.Matches(pixels));
  ASSERT_TRUE(observer.Ack());
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_TRUE(observer.Coherent());
}
TEST_F(RoundFive, SuppressOutput) {
  Open();
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client, false);
  FrameObserver observer(client);
  auto*         update   = client.Instance()->context->update;
  SuppressAndCheckInput(client);
  if (::testing::Test::HasFatalFailure()) return;
  auto                bytes  = client.Received();
  std::vector<UINT32> pixels(640uz * 480, 0x123456);
  Present(pixels, 640, 480);
  std::ranges::fill(pixels, 0x654321);
  Present(pixels, 640, 480);
  ThenSuppressed(client, observer, bytes);
  if (::testing::Test::HasFatalFailure()) return;
  RECTANGLE_16 const area{ 0, 0, 639, 479 };
  ASSERT_TRUE(update->SuppressOutput(client.Instance()->context, 1, &area));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  EXPECT_TRUE(client.Matches(pixels));
}
TEST_F(RoundFive, AspectAndMouse) {
  Open(640, 350, { 4, 3 });
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client, false);
  ThenAspectGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  FrameObserver       observer(client);
  std::vector<UINT32> pixels(640uz * 350);
  std::fill_n(pixels.begin() + 175uz * 640, 640, 0xffffff);
  Present(pixels, 640, 350);
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty(); }));
  ThenScaledHighlight(client);
  ThenAspectMouse(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_aspect(backend.get(), { 0, 0 }), 0);
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->height == 350 && client.Matches(pixels); }));
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
}
TEST_F(RoundFive, SparseRegions) {
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC }) {
    Open(1024, 768, { }, codec);
    Client client(sdlrdp_port(backend.get()), true, 1024, 768);
    Connect(client, false);
    FrameObserver       observer(client);
    std::vector<UINT32> pixels(1024uz * 768);
    std::ranges::generate(pixels, [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
    auto bytes = client.Received();
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 2; }));
    auto bounding = client.Received() - bytes;
    RecordProperty("bounding_bytes_" + std::to_string(codec), std::to_string(bounding));
    ThenSparseDamage(client, observer, pixels, bounding, codec);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

namespace {
bool WaitForAcknowledgement(Backend::State& state) {
  Expects(state.current != nullptr, "active peer owns the pending frame");
  std::unique_lock lock(state.frame_guard);
  return state.frame_changed.wait_for(lock, std::chrono::seconds(10),
                                      [&] { return state.current->acknowledged >= state.presented; });
}
}
TEST_F(RoundFive, WaitWithoutRefreshFeedback) {
  Expects(backend == nullptr, "backend has not opened");
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  ASSERT_EQ(Events(2).size(), 2u);
  FrameObserver             observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x445566);
  for (unsigned i = 1; i <= 20; ++i) {
    WhenAcknowledgedFrame(client, observer, pixels, i, WaitForAcknowledgement);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
TEST_F(RoundFive, NeverAcknowledges) {
  Open(320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver             observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x778899);
  auto                      start    = Clock::now();
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_GE(Clock::now() - start, std::chrono::milliseconds(200));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
}
TEST_F(RoundFive, ColourDepths) {
  Open(320, 200);
  for (auto depth : { 16u, 24u }) {
    ThenColourDepth(depth);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

namespace {
void ProduceFrames(sdlrdp_handle* backend, std::atomic<unsigned>& presents, std::stop_token const& stop) {
  std::vector<UINT32> pixels(1024uz * 768);
  sdlrdp_rect const   area  { 0, 0, 1024, 768 };
  while (!stop.stop_requested()) {
    auto sequence = presents.load() + 1;
    std::fill_n(pixels.begin(), 1024, sequence);
    std::fill_n(pixels.end() - 1024, 1024, sequence);
    Expects(sdlrdp_present(backend, pixels.data(), 4096, 1024, 768, &area, 1) == 0, "concurrent present accepted");
    presents = sequence;
    std::this_thread::yield();
  }
}
}
TEST_F(RoundFive, ProducerDoesNotStarveOrTear) {
  Open(1024, 768);
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver         observer(client);
  std::atomic<unsigned> presents = 0;
  std::jthread          producer([&](std::stop_token const& stop) { ProduceFrames(backend.get(), presents, stop); });
  for (unsigned i = 0; i < 20; ++i) {
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
  Open(320, 200, { }, SDLRDP_CODEC_AUTO);
  Client const client(sdlrdp_port(backend.get()), true);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  auto events    = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CONNECTED; });
  });
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_REMOTEFX);
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
void ThenDisconnectReason(Logs& logs, wLog* peer, char const* name) {
  WLog_Print(peer, WLOG_ERROR, "%s [0x00010000]", name);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, name));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, name));
}
void ThenTransportMessage(Logs& logs, char const* category, char const* message) {
  WLog_Print(WLog_Get(category), WLOG_ERROR, "%s", message);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, message));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, message));
}
void ThenExpectedTransportLogs(Logs& logs) {
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
  Open();
  ThenExpectedTransportLogs(logs);
  auto*       peer    = WLog_Get("com.freerdp.core.peer");
  auto const* failure = "BIO_write returned a system error 5: Input/output error";
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

TEST_F(RoundFive, GraphicsDisconnectDuringWrite) {
  constexpr unsigned side = 2048;
  Open(side, side, { }, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, side, side);
  ConnectPipeline(client);
  if (::testing::Test::HasFatalFailure()) return;
  Events();
  std::vector<UINT32> pixels(static_cast<std::size_t>(side) * side);
  Present(pixels, side, side);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  Headless::NoisePattern(pixels, 1);
  Present(pixels, side, side);
  if (::testing::Test::HasFatalFailure()) return;
  ThenReadable(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenWriteDisconnect(client);
}
}
