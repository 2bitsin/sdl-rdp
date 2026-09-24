#include "_detail/test-audio-gate.hpp"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <string>
#include <thread>

namespace BackendGate {
auto AudioGate::GivenConfirmingSession() -> void {
  GivenUnconfirmedSession();
  if (::testing::Test::HasFatalFailure()) return;
  EstablishConfirmations(ClientSession(), AudioSession());
}
auto AudioGate::ConnectAudioFormats(Client& client, SoundClient& audio) -> void {
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAudioFormats(audio);
}
auto AudioGate::ThenInitialVolume(Client& client, SoundClient& audio) -> void {
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
auto AudioGate::ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                   unsigned frames) -> void {
  EXPECT_TRUE(freerdp_disconnect(client.Instance().get()));
  EXPECT_EQ(writing.get(), frames);
  if (reconnect) EXPECT_EQ(audio.CaptureState().samples.size(), 1920u);
}
auto AudioGate::ThenFirstAudioBlockConfirms() -> void {
  ASSERT_TRUE(AudioSession().Confirm());
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_WARN, "Audio confirmation gate waiting"), 1u);
}
auto AudioGate::WhenLastAudioBlockConfirms(std::vector<INT16> const& pcm) -> void {
  ASSERT_TRUE(AudioSession().Confirm(24));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == 49920; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
}
auto AudioGate::WhenIdleAudioBurst(std::vector<INT16> const& pcm, unsigned burst) -> void {
  auto started  = Clock::now();
  auto writing  = std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), 48000); });
  auto captured = ClientSession().Until(
      [&] { return AudioSession().CaptureState().samples.size() == burst * pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 48000);
  EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
  if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
auto AudioGate::ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                       unsigned frames) -> void {
  auto received = client.Until([&] { return audio.CaptureState().samples.size() >= (reconnect ? 1920u : 48000u); });
  EXPECT_TRUE(received);
  if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  ThenWriterFinishes(client, audio, writing, reconnect, frames);
}
auto AudioGate::ThenSlowAudioConfirms(std::future<int>& writing) -> void {
  if (AudioSession().CaptureState().confirmed_frames < 480000) sdlrdp_audio_close(backend.get());
  EXPECT_EQ(AudioSession().CaptureState().confirmed_frames, 480000u);
  EXPECT_LE(AudioSession().CaptureState().maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
  RecordProperty("audio_diagnostics", logs.Text(true));
  EXPECT_EQ(writing.get(), 480000);
  RecordProperty("maximum_unconfirmed_ms",
                 std::to_string(double(AudioSession().CaptureState().maximum_pending_frames) / 48.0));
}
auto AudioGate::ThenInitialVolumeSamples(SoundClient const& audio) -> void {
  for (auto frame : audio.CaptureState().samples | std::views::chunk(2)) {
    EXPECT_EQ(frame[0], -12000);
    EXPECT_EQ(frame[1], 6000);
  }
}
auto AudioGate::ThenCapturedPcm(SoundClient const& audio, std::vector<INT16> const& pcm) -> void {
  EXPECT_EQ(audio.CaptureState().samples.size(), pcm.size());
  EXPECT_EQ(audio.CaptureState().samples.front(), pcm.front());
  EXPECT_EQ(audio.CaptureState().samples.back(), pcm.back());
  EXPECT_TRUE(std::ranges::equal(audio.CaptureState().samples, pcm));
  EXPECT_EQ(audio.CaptureState().pending.size(), 0u);
}
auto AudioGate::ThenMissingAudioHandle() -> void {
  sdlrdp_audio_close(nullptr);
  EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
  EXPECT_STREQ(sdlrdp_last_error(), "Invalid audio handle.");
  EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
}
auto AudioGate::GivenUnconfirmedSession() -> void {
  GivenAudioServer();
  if (::testing::Test::HasFatalFailure()) return;
  connected_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
  connected_audio  = std::make_unique<SoundClient>(*connected_client);
  GivenUnconfirmedAudio(*connected_client, *connected_audio);
}
auto AudioGate::ClientSession() -> Client& {
  return *connected_client;
}
auto AudioGate::AudioSession() -> SoundClient& {
  return *connected_audio;
}
}
