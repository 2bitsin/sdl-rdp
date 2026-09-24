#include "_detail/test-audio-gate.hpp"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <string>
#include <thread>

namespace BackendGate {
void AudioGate::GivenConfirmingSession() {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  EstablishConfirmations(ClientSession(), AudioSession());
}
void AudioGate::ConnectAudioFormats(Client& client, SoundClient& audio) {
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAudioFormats(audio);
}
void AudioGate::ThenInitialVolume(Client& client, SoundClient& audio) {
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
  std::vector<INT16> pcm(882uz * 2);
  std::ranges::generate(pcm, [i = 0]() mutable { return ++i % 2 ? -12000 : 12000; });
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 44), 44);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data() + 88, 838), 838);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  ThenInitialVolumeSamples(audio);
  if (::testing::Test::HasFatalFailure()) return;
  RecordProperty("volume_pcm", "44100 Hz; 44+838 frames; left=-12000 right=6000; volume=0x8000ffff");
}
void AudioGate::ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                   unsigned frames) {
  EXPECT_TRUE(freerdp_disconnect(client.Instance().get()));
  EXPECT_EQ(writing.get(), frames);
  if (reconnect) EXPECT_EQ(audio.CaptureState().samples.size(), 1920u);
}
void AudioGate::ThenFirstAudioBlockConfirms() {
  ASSERT_TRUE(AudioSession().Confirm());
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_WARN, "Audio confirmation gate waiting"), 1u);
}
void AudioGate::WhenLastAudioBlockConfirms(std::vector<INT16> const& pcm) {
  ASSERT_TRUE(AudioSession().Confirm(24));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == 49920; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
}
void AudioGate::WhenIdleAudioBurst(std::vector<INT16> const& pcm, unsigned burst) {
  auto started  = Clock::now();
  auto writing  = std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), 48000); });
  auto captured =
      ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == burst * pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 48000);
  EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
  if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
void AudioGate::ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                       unsigned frames) {
  auto received = client.Until([&] { return audio.CaptureState().samples.size() >= (reconnect ? 1920u : 48000u); });
  EXPECT_TRUE(received);
  if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  ThenWriterFinishes(client, audio, writing, reconnect, frames);
}
void AudioGate::ThenSlowAudioConfirms(std::future<int>& writing) {
  if (AudioSession().CaptureState().confirmed_frames < 480000) sdlrdp_audio_close(backend.get());
  EXPECT_EQ(AudioSession().CaptureState().confirmed_frames, 480000u);
  EXPECT_LE(AudioSession().CaptureState().maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
  RecordProperty("audio_diagnostics", logs.Text(true));
  EXPECT_EQ(writing.get(), 480000);
  RecordProperty("maximum_unconfirmed_ms",
                 std::to_string(double(AudioSession().CaptureState().maximum_pending_frames) / 48.0));
}
void AudioGate::ThenInitialVolumeSamples(SoundClient const& audio) {
  for (auto frame : audio.CaptureState().samples | std::views::chunk(2)) {
    EXPECT_EQ(frame[0], -12000);
    EXPECT_EQ(frame[1], 6000);
  }
}
void AudioGate::ThenCapturedPcm(SoundClient const& audio, std::vector<INT16> const& pcm) {
  EXPECT_EQ(audio.CaptureState().samples.size(), pcm.size());
  EXPECT_EQ(audio.CaptureState().samples.front(), pcm.front());
  EXPECT_EQ(audio.CaptureState().samples.back(), pcm.back());
  EXPECT_TRUE(std::ranges::equal(audio.CaptureState().samples, pcm));
  EXPECT_EQ(audio.CaptureState().pending.size(), 0u);
}
void AudioGate::ThenMissingAudioHandle() {
  sdlrdp_audio_close(nullptr);
  EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
  EXPECT_STREQ(sdlrdp_last_error(), "Invalid audio handle.");
  EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
}
void AudioGate::GivenUnconfirmedSession() {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  connected_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
  connected_audio  = std::make_unique<SoundClient>(*connected_client);
  GivenUnconfirmedAudio(*connected_client, *connected_audio);
}
Client& AudioGate::ClientSession() {
  return *connected_client;
}
SoundClient& AudioGate::AudioSession() {
  return *connected_audio;
}
}
