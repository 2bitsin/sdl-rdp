#include <sdl-rdp/headless-client.test/audio-gate.hpp>
#include <sdl-rdp/headless-client.test/backend-instance.hpp>
#include <sdl-rdp/headless-client.test/graphics-observer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace BackendGate {
TEST_F(AudioGate, AudioPlaybackConfirmsKeepRealtimeStreamContinuous) {
  ASSERT_NO_FATAL_FAILURE(GivenUnconfirmedSession());
  ASSERT_NO_FATAL_FAILURE(RunRealtimeAudio(ClientSession(), AudioSession()));
  ClientSession().Disconnect();
  backend.Close();
  CheckAudioStatistics(AudioSession());
}
namespace {
auto ObserveProgressivePayload(RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> std::uint32_t {
  Expects(channel, "channel is installed");
  Expects(command, "wire command is supplied");
  Expects(command->codecId == RDPGFX_CODECID_CAPROGRESSIVE, "payload uses the progressive codec");
  return CHANNEL_RC_OK;
}
auto ProduceProgressiveFrames(Headless::BackendInstance const& backend) -> std::size_t {
  std::vector<std::uint32_t> pixels(1280uz * 800);
  sdlrdp_rect const          full      { 0, 0, 1280, 800 };
  auto                       deadline  = Clock::now() + std::chrono::seconds(2);
  std::size_t                presented = 0;
  while (Clock::now() < deadline) {
    if (!sdlrdp_wait_frame(backend.Handle(), 10)) continue;
    Headless::MovingTilePattern(pixels, 1280, 800, presented);
    if (backend.Present(pixels, 1280, 800, full)) break;
    ++presented;
  }
  return presented;
}
}
TEST_F(AudioGate, AudioContinuousUnderProgressiveLoad) {
  ASSERT_NO_FATAL_FAILURE(Open(1280, 800, { }, SDLRDP_CODEC_PROGRESSIVE));
  ASSERT_EQ(sdlrdp_audio_open(backend.Handle()), 0);
  auto [client, audio] = NewSession(1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver const observer(client);
  ASSERT_NO_FATAL_FAILURE(GivenUnconfirmedAudio(client, audio));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.Channel()->SurfaceCommand = ObserveProgressivePayload;
  auto presenting = std::async(std::launch::async, [&] { return ProduceProgressiveFrames(backend); });
  ASSERT_NO_FATAL_FAILURE(RunRealtimeAudio(client, audio));
  EXPECT_GE(presenting.get(), 10u);
  EXPECT_GE(observer.Observed().frames.size(), 10u);
  client.Disconnect();
  backend.Close();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioNeverConfirmsUsesServerClock) {
  ASSERT_NO_FATAL_FAILURE(GivenUnconfirmedSession());
  std::vector<std::int16_t> const pcm(48000uz * 2, 1234);
  auto                            started = Clock::now();
  auto writing = std::async(std::launch::async,
                            [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data(), 48000); });
  EXPECT_TRUE(UntilCaptured(pcm.size()));
  EXPECT_EQ(writing.get(), 48000);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_GE(elapsed, 0.9);
  EXPECT_TRUE(logs.Contains("500"));
  RecordProperty("never_confirms_one_second_elapsed", std::to_string(elapsed));
}
}
