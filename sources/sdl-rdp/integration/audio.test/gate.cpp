#include <sdl-rdp/headless-client.test/audio/gate.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace BackendGate {
TEST_F(AudioGate, AudioAbsentDiscards) {
  ASSERT_NO_FATAL_FAILURE(ThenMissingAudioHandle());
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  std::vector<std::int16_t> frames(static_cast<std::ptrdiff_t>(48000 * 10) * 2, 1234);
  EXPECT_EQ(sdlrdp_audio_write(backend.Handle(), frames.data(), 480000), 480000);
  EXPECT_EQ(sdlrdp_audio_wait(backend.Handle(), 0), 1);
  sdlrdp_audio_close(backend.Handle());
}
TEST_F(AudioGate, AudioPcmAndReconnect) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  for (std::size_t connection = 0; connection < 2; ++connection) {
    auto [client, audio] = NewSession();
    ASSERT_NO_FATAL_FAILURE(ConnectAudioFormats(client, audio));
    auto                      frames = audio.CaptureState().rate / 50;
    std::vector<std::int16_t> pcm(std::size_t{ frames } * 2);
    std::ranges::iota(pcm, -480);
    ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), frames), frames);
    ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() >= pcm.size(); }));
    ASSERT_NO_FATAL_FAILURE(ThenCapturedPcm(audio, pcm));
  }
  RecordProperty("audio_diagnostics", logs.Text(true));
}
TEST_F(AudioGate, AudioFormatMissKeepsSessionAndReconnects) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  for (bool const unmatched : { false, true }) {
    auto [client, audio] = NewSession();
    audio.CaptureState().rate                = 22050;
    audio.CaptureState().advertise_unmatched = unmatched;
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    ASSERT_NO_FATAL_FAILURE(ThenUnavailableAudio(client, unmatched));
    ASSERT_NO_FATAL_FAILURE(ThenLiveVideoAndInput(client));
  }
  auto [client, audio] = NewSession();
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.Handle()), 44100u);
}
TEST_F(AudioGate, AudioBothRatesPrefer44100) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  auto [client, audio] = NewSession();
  audio.CaptureState().advertise_both_rates = true;
  ASSERT_NO_FATAL_FAILURE(ConnectAudioFormats(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.Handle()), 44100u);
  auto                      frames = audio.CaptureState().rate / 50;
  std::vector<std::int16_t> pcm(std::size_t{ frames } * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), frames), frames);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  EXPECT_EQ(audio.CaptureState().samples, pcm);
}
TEST_F(AudioGate, AudioInitialVolume) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  EXPECT_EQ(sdlrdp_audio_rate(backend.Handle()), 0u);
  auto [client, audio] = NewSession();
  audio.CaptureState().rate   = 44100;
  audio.CaptureState().volume = 0x8000ffffu;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ThenInitialVolume(client, audio);
}
TEST_F(AudioGate, AudioSlowConfirmsBoundTenSeconds) {
  ASSERT_NO_FATAL_FAILURE(GivenConfirmingSession());
  std::vector<std::int16_t> pcm(480000uz * 2, 1234);
  ConfirmationPace const    pace{ .frames  = 480000,
                                  .delay   = std::chrono::milliseconds(80),
                                  .timeout = std::chrono::seconds(15) };
  auto writing = std::async(std::launch::async,
                            [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data(), 480000); });
  ConfirmDelayedAudio(ClientSession(), AudioSession(), pace);
  ThenSlowAudioConfirms(writing);
}
TEST_F(AudioGate, AudioDisconnectDuringBlockedWrite) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  for (bool const reconnect : { false, true }) {
    auto [client, audio] = NewSession();
    audio.CaptureState().rate         = 48000;
    audio.CaptureState().auto_confirm = reconnect;
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
    std::uint32_t             frames = reconnect ? 960 : 480000;
    std::vector<std::int16_t> pcm(std::size_t{ frames } * 2, 1234);
    auto writing = std::async(std::launch::async,
                              [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data(), frames); });
    ASSERT_NO_FATAL_FAILURE(ThenDisconnectedWriter(client, audio, writing, reconnect, frames));
  }
}
TEST_F(AudioGate, AudioOneMillisecondPartialBlock) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, SDLRDP_CODEC_RAW, 1));
  ASSERT_EQ(sdlrdp_audio_open(backend.Handle()), 0);
  auto [client, audio] = NewSession();
  audio.CaptureState().rate = 48000;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::array<std::int16_t, 1920> pcm{ };
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), 48), 48);
  auto writing = std::async(std::launch::async,
                            [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data() + 96, 912); });
  EXPECT_TRUE(UntilCaptured(pcm.size()));
  EXPECT_EQ(writing.get(), 912);
}
TEST_F(AudioGate, AudioFallbackIdleDoesNotAccumulateCredit) {
  ASSERT_NO_FATAL_FAILURE(GivenUnconfirmedSession());
  std::vector<std::int16_t> const pcm(48000uz * 2, 1234);
  for (std::size_t burst = 1; burst <= 2; ++burst) {
    ASSERT_NO_FATAL_FAILURE(WhenIdleAudioBurst(pcm, burst));
  }
}

TEST_F(AudioGate, AudioReorderedConfirmsCreditOnlyTheirBlock) {
  ASSERT_NO_FATAL_FAILURE(GivenConfirmingSession());
  std::vector<std::int16_t> const pcm(24000uz * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), 24000), 24000);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().pending.size() == 25; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.Handle(), 0), 0);
  ASSERT_NO_FATAL_FAILURE(WhenLastAudioBlockConfirms(pcm));
  ThenFirstAudioBlockConfirms();
}
}
