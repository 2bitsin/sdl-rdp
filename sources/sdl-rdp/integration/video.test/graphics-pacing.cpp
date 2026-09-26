#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>

#include <cstddef>
#include <cstdint>
#include <regex>

namespace sdl_rdp::integration::video_test::detail::graphics_pacing {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::headless_client_test::graphics::RoundFive;

class PipelinedGraphics : public RoundFive {
protected:
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(RoundFive::SetUp());
    ASSERT_NO_FATAL_FAILURE(GivenPipelinedGraphics());
  }
  Pixels const pixels = Pixels(320uz * 200, 0x123456);
};
TEST_F(RoundFive, PipelinedLegacyPresent) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  Client client(backend.Port(), true);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  FrameObserver const observer(client);
  Pixels const        pixels(320uz * 200, 0x123456);
  ASSERT_NO_FATAL_FAILURE(ThenPipelinedWindow(client, observer.Frames(), pixels));
  ThenLegacyWindowReleases(client, observer, pixels);
}
TEST_F(PipelinedGraphics, PipelinedGraphicsPresent) {
  ASSERT_NO_FATAL_FAILURE(ThenPipelinedWindow(GraphicsClient(), Observer().Observed().frames, pixels));
  ThenGraphicsWindowReleases(pixels);
}

namespace {
auto ThenFrameStatistics(Logs& logs) -> void {
  EXPECT_EQ(logs.Count(LogLevel::Info, "Frames:"), 1u);
  EXPECT_TRUE(logs.Contains(LogLevel::Info, "Frames: 3 sent, 2 coalesced; encode ")) << logs.Text(true);
  auto text = logs.Text(true);
  EXPECT_TRUE(std::regex_search(
      text, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms, [0-9]+ timed out\.)")))
      << text;
  testing::Test::RecordProperty("statistics", text);
}
}
TEST_F(PipelinedGraphics, GraphicsFrameStatistics) {
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), Observer(), pixels, 1, 2));
  for (std::size_t count = 0; count < 3; ++count) Present(pixels, 320, 200);
  EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 1 }));
  ASSERT_TRUE(Observer().AckFrame(0, 0));
  ASSERT_NO_FATAL_FAILURE(AwaitFrames(GraphicsClient(), Observer().Observed().frames, 3));
  ASSERT_NO_FATAL_FAILURE(ThenGraphicsAcknowledgementsCounted());
  GraphicsClient().Disconnect();
  backend.Close();
  ThenFrameStatistics(logs);
}
TEST_F(PipelinedGraphics, GraphicsAcknowledgementsAgeOutAndResume) {
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), Observer(), pixels, 1, 4));
  ASSERT_TRUE(Observer().AckFrame(3, 0));
  ASSERT_TRUE(GraphicsClient().Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }); }));
  ASSERT_NO_FATAL_FAILURE(PresentGraphicsFrames(GraphicsClient(), Observer(), pixels, 5, 6));
  ThenAgedWindowResumes(pixels);
}
}
