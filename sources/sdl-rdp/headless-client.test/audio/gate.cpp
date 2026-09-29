#include <sdl-rdp/headless-client.test/audio/gate.hpp>

#include <sdl-rdp/diagnostics/log-sink.hpp>

#include <oxbox/utilities/chunk.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

namespace sdl_rdp::headless_client_test::audio::detail::gate {
using oxbox::utilities::Chunk;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::audio::WriteFrames;
using sdl_rdp::headless_client_test::backend::Clock;

namespace {
auto ThenInitialVolumeSamples(SoundClient const& audio) -> void {
  for (auto frame : audio.CaptureState().samples | Chunk(2)) {
    EXPECT_EQ(frame[0], -12000);
    EXPECT_EQ(frame[1], 6000);
  }
}
}
auto ThenCapturedPcm(SoundClient const& audio, std::vector<std::int16_t> const& pcm) -> void {
  EXPECT_EQ(audio.CaptureState().samples.size(), pcm.size());
  EXPECT_EQ(audio.CaptureState().samples.front(), pcm.front());
  EXPECT_EQ(audio.CaptureState().samples.back(), pcm.back());
  EXPECT_TRUE(std::ranges::equal(audio.CaptureState().samples, pcm));
  EXPECT_EQ(audio.CaptureState().pending.size(), 0u);
}
auto AudioGate::GivenConfirmingSession() -> void {
  ASSERT_NO_FATAL_FAILURE(GivenUnconfirmedSession());
  EstablishConfirmations(ClientSession(), AudioSession());
}
auto AudioGate::ConnectAudioFormats(Client& client, SoundClient& audio) -> void {
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ThenAudioFormats(audio);
}
auto AudioGate::ThenInitialVolume(Client& client, SoundClient& audio) -> void {
  EXPECT_EQ((*backend).Audio().Rate(), 44100u);
  std::vector<std::int16_t> pcm(882uz * 2);
  std::ranges::generate(pcm, [i = 0]() mutable { return ++i % 2 ? -12000 : 12000; });
  ASSERT_EQ(WriteFrames(*backend, pcm, 0, 44), 44);
  ASSERT_EQ(WriteFrames(*backend, pcm, 44, 838), 838);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  ASSERT_NO_FATAL_FAILURE(ThenInitialVolumeSamples(audio));
  RecordProperty("volume_pcm", "44100 Hz; 44+838 frames; left=-12000 right=6000; volume=0x8000ffff");
}
auto AudioGate::ThenWriterFinishes(Client& client, SoundClient& audio, std::future<std::size_t>& writing,
                                   bool reconnect, std::uint32_t frames) -> void {
  EXPECT_TRUE(client.Disconnect());
  EXPECT_EQ(writing.get(), frames);
  if (reconnect) EXPECT_EQ(audio.CaptureState().samples.size(), 1920u);
}
auto AudioGate::ThenFirstAudioBlockConfirms() -> void {
  ASSERT_TRUE(AudioSession().Confirm());
  EXPECT_TRUE(backend.WaitAudio(std::chrono::milliseconds{ 10000 }));
  EXPECT_EQ(logs.Count(LogLevel::Warn, "Audio confirmation gate waiting"), 1u);
}
auto AudioGate::WhenLastAudioBlockConfirms(std::vector<std::int16_t> const& pcm) -> void {
  ASSERT_TRUE(AudioSession().Confirm(24));
  EXPECT_TRUE(backend.WaitAudio(std::chrono::milliseconds{ 10000 }));
  ASSERT_EQ(WriteFrames(*backend, pcm, 0, 960), 960);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == 49920; }));
  EXPECT_FALSE(backend.WaitAudio(std::chrono::milliseconds{ 0 }));
}
auto AudioGate::WhenIdleAudioBurst(std::vector<std::int16_t> const& pcm, std::size_t burst) -> void {
  auto started = Clock::now();
  auto writing = std::async(std::launch::async, [&] { return WriteFrames(*backend, pcm, 0, 48000); });
  EXPECT_TRUE(UntilCaptured(burst * pcm.size()));
  EXPECT_EQ(writing.get(), 48000);
  EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
  if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
auto AudioGate::ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<std::size_t>& writing,
                                       bool reconnect, std::uint32_t frames) -> void {
  auto received = client.Until([&] { return audio.CaptureState().samples.size() >= (reconnect ? 1920u : 48000u); });
  EXPECT_TRUE(received);
  if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  ThenWriterFinishes(client, audio, writing, reconnect, frames);
}
auto AudioGate::ThenSlowAudioConfirms(std::future<std::size_t>& writing) -> void {
  if (AudioSession().CaptureState().confirmed_frames < 480000) (*backend).Audio().Close();
  EXPECT_EQ(AudioSession().CaptureState().confirmed_frames, 480000u);
  EXPECT_LE(AudioSession().CaptureState().maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(LogLevel::Warn, "Audio confirmation gate waiting: client is 500.000 ms behind."));
  RecordProperty("audio_diagnostics", logs.Text(true));
  EXPECT_EQ(writing.get(), 480000);
  RecordProperty("maximum_unconfirmed_ms",
                 std::to_string(static_cast<double>(AudioSession().CaptureState().maximum_pending_frames) / 48.0));
}
auto AudioGate::GivenUnconfirmedSession() -> void {
  ASSERT_NO_FATAL_FAILURE(GivenAudioServer());
  auto [client, audio] = NewSession();
  GivenUnconfirmedAudio(client, audio);
}
auto AudioGate::NewSession(std::uint32_t width, std::uint32_t height) -> std::pair<Client&, SoundClient&> {
  connected_audio.reset();
  connected_client = std::make_unique<Client>(backend.Port(), true, width, height);
  connected_audio  = std::make_unique<SoundClient>(*connected_client);
  return { *connected_client, *connected_audio };
}
// A capture that never completes closes the device, so a writer blocked on it returns.
auto AudioGate::UntilCaptured(std::size_t samples) -> bool {
  auto const captured = ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == samples; });
  if (!captured) (*backend).Audio().Close();
  return captured;
}
auto AudioGate::ClientSession() -> Client& {
  return *connected_client;
}
auto AudioGate::AudioSession() -> SoundClient& {
  return *connected_audio;
}
}
