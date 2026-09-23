#include "_detail/test-backend.hpp"

namespace BackendGate {
TEST_F(RoundFive, PipelinedLegacyPresent) {
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver const observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  AwaitFrames(client, observer.Frames(), 1);
  if (::testing::Test::HasFatalFailure()) return;
  Present(pixels, 320, 200);
  AwaitFrames(client, observer.Frames(), 2);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ThenLegacyWindowReleases(client, observer, pixels);
}
TEST_F(RoundFive, PipelinedGraphicsPresent) {
  GivenPipelinedGraphics();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 1);
  if (::testing::Test::HasFatalFailure()) return;
  Present(pixels, 320, 200);
  AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 2);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ThenGraphicsWindowReleases(pixels);
}

namespace {
void ThenFrameStatistics(Logs& logs) {
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Frames: 3 sent, 2 coalesced; encode ")) << logs.Text(true);
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(
      text, match,
      std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, ([0-9]+) over 100 ms, ([0-9]+) timed out\.)")))
      << text;
  EXPECT_EQ(match[1], "0");
  EXPECT_EQ(match[2], "0");
  testing::Test::RecordProperty("statistics", logs.Text(true));
}
}
TEST_F(RoundFive, GraphicsFrameStatistics) {
  GivenPipelinedGraphics();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 1, 2);
  if (::testing::Test::HasFatalFailure()) return;
  for (unsigned count = 0; count < 3; ++count)
    Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ASSERT_TRUE(GraphicsObserver().AckFrame(0, 0));
  AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 3);
  if (::testing::Test::HasFatalFailure()) return;
  ThenGraphicsAcknowledgementsCounted();
  if (::testing::Test::HasFatalFailure()) return;
  freerdp_disconnect(GraphicsClient().Instance().get());
  backend.reset();
  ThenFrameStatistics(logs);
}
TEST_F(RoundFive, GraphicsAcknowledgementsAgeOutAndResume) {
  GivenPipelinedGraphics();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 1, 4);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(GraphicsObserver().AckFrame(3, 0));
  ASSERT_TRUE(GraphicsClient().Until([&] { return sdlrdp_wait_frame(backend.get(), 0) != 0; }));
  PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 5, 6);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAgedWindowResumes(pixels);
}
}
