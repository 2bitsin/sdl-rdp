#include <sdl-rdp/headless-client.test/audio/gate.hpp>
#include <sdl-rdp/abi/backend.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <thread>

namespace sdl_rdp::headless_client_test::audio::detail::gate {
using sdl_rdp::headless_client_test::backend::Clock;

namespace {
auto ThenInitialVolumeSamples(SoundClient const& audio) -> void {
  for (auto frame : audio.CaptureState().samples | std::views::chunk(2)) {
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
auto ThenMissingAudioHandle() -> void {
  sdlrdp_audio_close(nullptr);
  EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
  EXPECT_STREQ(sdlrdp_last_error(), "Audio handle is null.");
  EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
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
  EXPECT_EQ(sdlrdp_audio_rate(backend.Handle()), 44100u);
  std::vector<std::int16_t> pcm(882uz * 2);
  std::ranges::generate(pcm, [i = 0]() mutable { return ++i % 2 ? -12000 : 12000; });
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), 44), 44);
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data() + 88, 838), 838);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() == pcm.size(); }));
  ASSERT_NO_FATAL_FAILURE(ThenInitialVolumeSamples(audio));
  RecordProperty("volume_pcm", "44100 Hz; 44+838 frames; left=-12000 right=6000; volume=0x8000ffff");
}
auto AudioGate::ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                   std::uint32_t frames) -> void {
  EXPECT_TRUE(client.Disconnect());
  EXPECT_EQ(writing.get(), frames);
  if (reconnect) EXPECT_EQ(audio.CaptureState().samples.size(), 1920u);
}
auto AudioGate::ThenFirstAudioBlockConfirms() -> void {
  ASSERT_TRUE(AudioSession().Confirm());
  EXPECT_EQ(sdlrdp_audio_wait(backend.Handle(), 10000), 1);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_WARN, "Audio confirmation gate waiting"), 1u);
}
auto AudioGate::WhenLastAudioBlockConfirms(std::vector<std::int16_t> const& pcm) -> void {
  ASSERT_TRUE(AudioSession().Confirm(24));
  EXPECT_EQ(sdlrdp_audio_wait(backend.Handle(), 10000), 1);
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), 960), 960);
  ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == 49920; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.Handle(), 0), 0);
}
auto AudioGate::WhenIdleAudioBurst(std::vector<std::int16_t> const& pcm, std::size_t burst) -> void {
  auto started = Clock::now();
  auto writing = std::async(std::launch::async,
                            [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data(), 48000); });
  EXPECT_TRUE(UntilCaptured(burst * pcm.size()));
  EXPECT_EQ(writing.get(), 48000);
  EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
  if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
auto AudioGate::ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                       std::uint32_t frames) -> void {
  auto received = client.Until([&] { return audio.CaptureState().samples.size() >= (reconnect ? 1920u : 48000u); });
  EXPECT_TRUE(received);
  if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  ThenWriterFinishes(client, audio, writing, reconnect, frames);
}
auto AudioGate::ThenSlowAudioConfirms(std::future<int>& writing) -> void {
  if (AudioSession().CaptureState().confirmed_frames < 480000) sdlrdp_audio_close(backend.Handle());
  EXPECT_EQ(AudioSession().CaptureState().confirmed_frames, 480000u);
  EXPECT_LE(AudioSession().CaptureState().maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
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
  connected_client = std::make_unique<Client>(sdlrdp_port(backend.Handle()), true, width, height);
  connected_audio  = std::make_unique<SoundClient>(*connected_client);
  return { *connected_client, *connected_audio };
}
// A capture that never completes closes the device, so a writer blocked on it returns.
auto AudioGate::UntilCaptured(std::size_t samples) -> bool {
  auto const captured = ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == samples; });
  if (!captured) sdlrdp_audio_close(backend.Handle());
  return captured;
}
auto AudioGate::ClientSession() -> Client& {
  return *connected_client;
}
auto AudioGate::AudioSession() -> SoundClient& {
  return *connected_audio;
}
}
