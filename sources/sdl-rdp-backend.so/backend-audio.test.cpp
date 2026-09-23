#include "_detail/test-audio.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace BackendGate {
TEST_F(AudioGate, AudioAbsentDiscards) {
  ThenMissingAudioHandle();
  if (::testing::Test::HasFatalFailure()) return;
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<INT16> frames(static_cast<std::ptrdiff_t>(48000 * 10) * 2, 1234);
  EXPECT_EQ(sdlrdp_audio_write(backend.get(), frames.data(), 480000), 480000);
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 1);
  sdlrdp_audio_close(backend.get());
}
TEST_F(AudioGate, AudioPcmAndReconnect) {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  for (unsigned connection = 0; connection < 2; ++connection) {
    Client      client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    ConnectAudioFormats(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
    auto frames = audio.CaptureState().rate / 50;
    std::vector<INT16> pcm(static_cast<std::size_t>(frames) * 2);
    std::ranges::iota(pcm, -480);
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), frames), frames);
    ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() >= pcm.size(); }));
    ThenCapturedPcm(audio, pcm);
    if (::testing::Test::HasFatalFailure()) return;
  }
  RecordProperty("audio_diagnostics", logs.Text(true));
}
TEST_F(AudioGate, AudioFormatMissKeepsSessionAndReconnects) {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  for (bool const unmatched : { false, true }) {
    Client      client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.CaptureState().rate                = 22050;
    audio.CaptureState().advertise_unmatched = unmatched;
    Connect(client);
    ThenUnavailableAudio(client, unmatched);
    ThenLiveVideoAndInput(client);
  }
  Client      client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
}
TEST_F(AudioGate, AudioBothRatesPrefer44100) {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  Client      client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.CaptureState().advertise_both_rates = true;
  ConnectAudioFormats(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
  auto frames = audio.CaptureState().rate / 50;
  std::vector<INT16> pcm(static_cast<std::size_t>(frames) * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), frames), frames);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  EXPECT_EQ(audio.CaptureState().samples, pcm);
}
TEST_F(AudioGate, AudioInitialVolume) {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
  Client      client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.CaptureState().rate   = 44100;
  audio.CaptureState().volume = 0x8000ffffu;
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ThenInitialVolume(client, audio);
}
TEST_F(AudioGate, AudioSlowConfirmsBoundTenSeconds) {
  GivenConfirmingSession();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<INT16> pcm(480000uz * 2, 1234);
  auto writing  = std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), 480000); });
  auto deadline = Clock::now() + std::chrono::seconds(15);
  while (AudioSession().CaptureState().confirmed_frames < 480000 && Clock::now() < deadline) {
    if (!ClientSession().Pump(2)) break;
    while (!AudioSession().CaptureState().pending.empty() &&
           Clock::now() - AudioSession().CaptureState().pending.front().received >= std::chrono::milliseconds(80))
      if (!AudioSession().Confirm()) break;
  }
  ThenSlowAudioConfirms(writing);
}
TEST_F(AudioGate, AudioPlaybackConfirmsKeepRealtimeStreamContinuous) {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  RunRealtimeAudio(ClientSession(), AudioSession());
  freerdp_disconnect(ClientSession().Instance().get());
  backend.reset();
  CheckAudioStatistics(AudioSession());
}
namespace {
UINT ObserveProgressivePayload(RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) {

  Expects(channel, "channel is installed");
  Expects(command, "wire command is supplied");
  Expects(command->codecId == RDPGFX_CODECID_CAPROGRESSIVE, "payload uses the progressive codec");
  return CHANNEL_RC_OK;
}
unsigned ProduceProgressiveFrames(sdlrdp_handle* backend) {
  std::vector<UINT32> pixels(1280uz * 800);
  sdlrdp_rect const full     { 0, 0, 1280, 800 };
  auto              deadline  = Clock::now() + std::chrono::seconds(2);
  unsigned          presented = 0;
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
TEST_F(AudioGate, AudioDisconnectDuringBlockedWrite) {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  for (bool const reconnect : { false, true }) {
    Client      client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.CaptureState().rate         = 48000;
    audio.CaptureState().auto_confirm = reconnect;
    ConnectAudio(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
    EstablishConfirmations(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
    unsigned frames = reconnect ? 960 : 480000;
    std::vector<INT16> pcm(static_cast<std::size_t>(frames) * 2, 1234);
    auto writing =
        std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), frames); });
    ThenDisconnectedWriter(client, audio, writing, reconnect, frames);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
TEST_F(AudioGate, AudioOneMillisecondPartialBlock) {
  Open(320, 200, { }, SDLRDP_CODEC_RAW, 1);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client      client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.CaptureState().rate = 48000;
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  std::array<INT16, 1920> pcm{ };
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 48), 48);
  auto writing =
      std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data() + 96, 912); });
  auto captured = client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 912);
}
TEST_F(AudioGate, AudioFallbackIdleDoesNotAccumulateCredit) {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<INT16> const pcm(48000uz * 2, 1234);
  for (unsigned burst = 1; burst <= 2; ++burst) {
    WhenIdleAudioBurst(pcm, burst);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

TEST_F(AudioGate, AudioReorderedConfirmsCreditOnlyTheirBlock) {
  GivenConfirmingSession();
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<INT16> const pcm(24000uz * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().pending.size() == 25; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  WhenLastAudioBlockConfirms(pcm);
  if (::testing::Test::HasFatalFailure()) return;
  ThenFirstAudioBlockConfirms();
}
}
