#include <sdl-rdp/headless-client.test/audio/gate.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/session/exceptions.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <tuple>

namespace sdl_rdp::integration::audio_test::detail::gate {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::audio::AudioGate;
using sdl_rdp::headless_client_test::audio::ConfirmationPace;
using sdl_rdp::headless_client_test::audio::ThenCapturedPcm;
using sdl_rdp::headless_client_test::audio::WriteFrames;
using sdl_rdp::session::AudioDeviceState;

TEST_F(AudioGate, DeviceStateIsAFailureToOpenTwiceOrWriteClosed) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  EXPECT_THROW((*backend).Audio().Open(), AudioDeviceState);
  (*backend).Audio().Close();
  std::vector<std::int16_t> const frames(960, 0);
  EXPECT_THROW(std::ignore = WriteFrames(*backend, frames, 0, 480), AudioDeviceState);
}
TEST_F(AudioGate, AudioAbsentDiscards) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  std::vector<std::int16_t> frames(static_cast<std::ptrdiff_t>(48000 * 10) * 2, 1234);
  EXPECT_EQ(WriteFrames(*backend, frames, 0, 480000), 480000);
  EXPECT_TRUE(backend.WaitAudio(std::chrono::milliseconds{ 0 }));
  (*backend).Audio().Close();
}
TEST_F(AudioGate, AudioPcmAndReconnect) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  for (std::size_t connection = 0; connection < 2; ++connection) {
    auto [client, audio] = NewSession();
    ASSERT_NO_FATAL_FAILURE(ConnectAudioFormats(client, audio));
    auto                      frames = audio.CaptureState().rate / 50;
    std::vector<std::int16_t> pcm(std::size_t{ frames } * 2);
    std::ranges::iota(pcm, -480);
    ASSERT_EQ(WriteFrames(*backend, pcm, 0, frames), frames);
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
  EXPECT_EQ((*backend).Audio().Rate(), 44100u);
}
TEST_F(AudioGate, AudioBothRatesPrefer44100) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  auto [client, audio] = NewSession();
  audio.CaptureState().advertise_both_rates = true;
  ASSERT_NO_FATAL_FAILURE(ConnectAudioFormats(client, audio));
  EXPECT_EQ((*backend).Audio().Rate(), 44100u);
  auto                      frames = audio.CaptureState().rate / 50;
  std::vector<std::int16_t> pcm(std::size_t{ frames } * 2, 1234);
  ASSERT_EQ(WriteFrames(*backend, pcm, 0, frames), frames);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  EXPECT_EQ(audio.CaptureState().samples, pcm);
}
TEST_F(AudioGate, AudioInitialVolume) {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  EXPECT_EQ((*backend).Audio().Rate(), 0u);
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
  auto writing = std::async(std::launch::async, [&] { return WriteFrames(*backend, pcm, 0, 480000); });
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
    auto writing = std::async(std::launch::async, [&] { return WriteFrames(*backend, pcm, 0, frames); });
    ASSERT_NO_FATAL_FAILURE(ThenDisconnectedWriter(client, audio, writing, reconnect, frames));
  }
}
TEST_F(AudioGate, AudioOneMillisecondPartialBlock) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, Codec::Raw, 1));
  (*backend).Audio().Open();
  auto [client, audio] = NewSession();
  audio.CaptureState().rate = 48000;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::array<std::int16_t, 1920> pcm{ };
  ASSERT_EQ(WriteFrames(*backend, pcm, 0, 48), 48);
  auto writing = std::async(std::launch::async, [&] { return WriteFrames(*backend, pcm, 48, 912); });
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
  ASSERT_EQ(WriteFrames(*backend, pcm, 0, 24000), 24000);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().pending.size() == 25; }));
  EXPECT_FALSE(backend.WaitAudio(std::chrono::milliseconds{ 0 }));
  ASSERT_NO_FATAL_FAILURE(WhenLastAudioBlockConfirms(pcm));
  ThenFirstAudioBlockConfirms();
}
}
