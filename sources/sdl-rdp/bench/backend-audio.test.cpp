#include <sdl-rdp/headless-client.test/audio-gate.hpp>
#include <sdl-rdp/headless-client.test/graphics-observer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace BackendGate {
TEST_F(AudioGate, AudioPlaybackConfirmsKeepRealtimeStreamContinuous) {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  RunRealtimeAudio(ClientSession(), AudioSession());
  freerdp_disconnect(ClientSession().Instance().get());
  backend.reset();
  CheckAudioStatistics(AudioSession());
}
namespace {
auto ObserveProgressivePayload(RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> UINT {
  Expects(channel, "channel is installed");
  Expects(command, "wire command is supplied");
  Expects(command->codecId == RDPGFX_CODECID_CAPROGRESSIVE, "payload uses the progressive codec");
  return CHANNEL_RC_OK;
}
auto ProduceProgressiveFrames(sdlrdp_handle* backend) -> unsigned {
  std::vector<UINT32> pixels(1280uz * 800);
  sdlrdp_rect const   full      { 0, 0, 1280, 800 };
  auto                deadline  = Clock::now() + std::chrono::seconds(2);
  unsigned            presented = 0;
  while (Clock::now() < deadline) {
    if (!sdlrdp_wait_frame(backend, 10)) continue;
    Headless::MovingTilePattern(pixels, 1280, 800, presented);
    if (sdlrdp_present(backend, pixels.data(), 5120, 1280, 800, &full, 1)) break;
    ++presented;
  }
  return presented;
}
}
TEST_F(AudioGate, AudioContinuousUnderProgressiveLoad) {
  Open(1280, 800, { }, SDLRDP_CODEC_PROGRESSIVE);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver const observer(client);
  SoundClient                      audio(client);
  GivenUnconfirmedAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.Channel()->SurfaceCommand = ObserveProgressivePayload;
  auto presenting = std::async(std::launch::async, [&] { return ProduceProgressiveFrames(backend.get()); });
  RunRealtimeAudio(client, audio);
  EXPECT_GE(presenting.get(), 10u);
  EXPECT_GE(observer.Observed().frames.size(), 10u);
  freerdp_disconnect(client.Instance().get());
  backend.reset();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioNeverConfirmsUsesServerClock) {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<INT16> const pcm(48000uz * 2, 1234);
  auto started  = Clock::now();
  auto writing  = std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), 48000); });
  auto captured = ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 48000);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_GE(elapsed, 0.9);
  EXPECT_TRUE(logs.Contains("500"));
  RecordProperty("never_confirms_one_second_elapsed", std::to_string(elapsed));
}
}
