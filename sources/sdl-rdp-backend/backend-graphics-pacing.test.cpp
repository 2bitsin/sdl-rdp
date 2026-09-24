#include <sdl-rdp/headless-client.test/graphics-observer.hpp>
#include <sdl-rdp/headless-client.test/round-five.hpp>

#include <regex>

namespace BackendGate {
class PipelinedGraphics : public RoundFive {
protected:
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(RoundFive::SetUp());
    ASSERT_NO_FATAL_FAILURE(GivenPipelinedGraphics());
  }
  std::vector<std::uint32_t> const pixels = std::vector<std::uint32_t>(320uz * 200, 0x123456);
};
TEST_F(RoundFive, PipelinedLegacyPresent) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  Client client(sdlrdp_port(backend.Handle()), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  FrameObserver const       observer(client);
  std::vector<UINT32> const pixels(320uz * 200, 0x123456);
  ASSERT_NO_FATAL_FAILURE(ThenPipelinedWindow(client, observer.Frames(), pixels));
  ThenLegacyWindowReleases(client, observer, pixels);
}
TEST_F(PipelinedGraphics, PipelinedGraphicsPresent) {
  ASSERT_NO_FATAL_FAILURE(ThenPipelinedWindow(GraphicsClient(), GraphicsObserver().Observed().frames, pixels));
  ThenGraphicsWindowReleases(pixels);
}

namespace {
auto ThenFrameStatistics(Logs& logs) -> void {
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Frames: 3 sent, 2 coalesced; encode ")) << logs.Text(true);
  auto text = logs.Text(true);
  EXPECT_TRUE(std::regex_search(
      text, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms, [0-9]+ timed out\.)")))
      << text;
  testing::Test::RecordProperty("statistics", text);
}
}
TEST_F(PipelinedGraphics, GraphicsFrameStatistics) {
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 1, 2));
  for (unsigned count = 0; count < 3; ++count) Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.Handle(), 1), 0);
  ASSERT_TRUE(GraphicsObserver().AckFrame(0, 0));
  ASSERT_NO_FATAL_FAILURE(AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 3));
  ASSERT_NO_FATAL_FAILURE(ThenGraphicsAcknowledgementsCounted());
  GraphicsClient().Disconnect();
  backend.Close();
  ThenFrameStatistics(logs);
}
TEST_F(PipelinedGraphics, GraphicsAcknowledgementsAgeOutAndResume) {
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 1, 4));
  ASSERT_TRUE(GraphicsObserver().AckFrame(3, 0));
  ASSERT_TRUE(GraphicsClient().Until([&] { return sdlrdp_wait_frame(backend.Handle(), 0) != 0; }));
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), GraphicsObserver(), pixels, 5, 6));
  ThenAgedWindowResumes(pixels);
}
}
