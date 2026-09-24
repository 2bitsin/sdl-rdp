#include <sdl-rdp/headless-client.test/audio-session.hpp>

#include <sdl-rdp/headless-client.test/tone-measurements.hpp>

#include <freerdp/input.h>
#include <oxbox/utilities/number-text.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <future>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace BackendGate {
namespace {
auto WriteRealtimeAudio(sdlrdp_handle* backend) -> int {
  std::array<INT16, 480uz * 2> pcm     { };
  auto                         start   = Clock::now();
  int                          written = 0;
  for (unsigned tick = 1; tick <= 200; ++tick) {
    std::this_thread::sleep_until(start + std::chrono::milliseconds(tick * 10));
    auto count = sdlrdp_audio_write(backend, pcm.data(), 480);
    if (count != 480) return written;
    written += count;
  }
  return written;
}
auto Due(SoundClient const& audio, std::chrono::milliseconds delay) -> bool {
  auto const& pending = audio.CaptureState().pending;
  return !pending.empty() && Clock::now() - pending.front().received >= delay;
}
auto ConfirmDue(SoundClient& audio, std::chrono::milliseconds delay) -> void {
  while (Due(audio, delay))
    if (!audio.Confirm()) return;
}
auto ThenAudioCadence(SoundClient const& audio) -> void {
  ASSERT_GT(audio.CaptureState().received.size(), 1u);
  auto const maximum_gap = Headless::MaximumGapMs(audio.CaptureState().received);
  auto       block_ms    = 1000.0 * double(audio.CaptureState().samples.size()) / 2
                           / double(audio.CaptureState().received.size()) / audio.CaptureState().rate;
  testing::Test::RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
  EXPECT_LE(maximum_gap, (2 * block_ms) + 10);
}
}
auto AudioSession::ConfirmDelayedAudio(Client& client, SoundClient& audio, ConfirmationPace pace) -> void {
  auto deadline = Clock::now() + pace.timeout;
  while (audio.CaptureState().confirmed_frames < pace.frames && Clock::now() < deadline) {
    if (!client.Pump(2)) break;
    ConfirmDue(audio, pace.delay);
  }
}
auto AudioSession::ThenLiveInput(Client& client) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
  auto const events = UntilEvent(client, SDLRDP_KEY);
  EXPECT_TRUE(std::ranges::contains(events, SDLRDP_KEY, &sdlrdp_event::type));
  EXPECT_FALSE(std::ranges::contains(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type));
}
auto AudioSession::ThenRealtimeCounts(SoundClient const& audio) -> void {
  EXPECT_EQ(audio.CaptureState().received.size(), 100u);
  EXPECT_FALSE(logs.Contains("Audio confirmation gate waiting"));
  EXPECT_EQ(audio.CaptureState().confirmed_frames, 96000u);
}
auto AudioSession::GivenAudioServer() -> void {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  ASSERT_EQ(sdlrdp_audio_open(backend.Handle()), 0);
}
auto AudioSession::ThenAudioFormats(SoundClient const& audio) -> void {
  ASSERT_EQ(audio.CaptureState().server_formats.size(), 2u);
  EXPECT_EQ(audio.CaptureState().server_formats[0].nSamplesPerSec, 44100u);
  EXPECT_EQ(audio.CaptureState().server_formats[1].nSamplesPerSec, 48000u);
}
auto AudioSession::GivenUnconfirmedAudio(Client& client, SoundClient& audio) -> void {
  audio.CaptureState().rate         = 48000;
  audio.CaptureState().auto_confirm = false;
  ConnectAudio(client, audio);
}
auto AudioSession::ConnectAudio(Client& client, SoundClient& audio) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().opened; }));
  auto const audio_connected = [](auto const& events) {
    return std::ranges::any_of(events,
                               [](auto const& event) { return event.type == SDLRDP_AUDIO && event.audio.connected; });
  };
  ASSERT_TRUE(audio_connected(EventsUntil(audio_connected, true, [&client] { return client.Pump(); }))) << logs.Text();
}
auto AudioSession::RunRealtimeAudio(Client& client, SoundClient& audio) -> void {
  Expects(backend.Handle() != nullptr, "backend exists");
  Expects(audio.CaptureState().opened, "client audio channel is open");
  auto writing = std::async(std::launch::async, [&] { return WriteRealtimeAudio(backend.Handle()); });
  ConfirmDelayedAudio(client, audio,
                      { .frames = 96000, .delay = std::chrono::milliseconds(150), .timeout = std::chrono::seconds(4) });
  sdlrdp_audio_close(backend.Handle());
  EXPECT_EQ(writing.get(), 96000);
  EXPECT_EQ(audio.CaptureState().samples.size() / 2, 96000u);
  ASSERT_NO_FATAL_FAILURE(ThenAudioCadence(audio));
  ThenRealtimeCounts(audio);
}
auto AudioSession::CheckAudioStatistics(SoundClient const& audio) -> void {
  Expects(!backend, "connection statistics have been flushed");
  auto        text  = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(
      text, match,
      std::regex(
          R"(Audio: ([0-9]+) blocks sent; gap ([0-9.]+) ms mean, ([0-9.]+) ms max; ([0-9]+) gaps over 40 ms\.)")))
      << text;
  EXPECT_EQ(oxbox::utilities::ParseNumber<std::size_t>(match.str(1)), audio.CaptureState().received.size());
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Audio:"), 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_TRUE(std::regex_search(
      text, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms, [0-9]+ timed out\.)")))
      << text;
}
auto AudioSession::EstablishConfirmations(Client& client, SoundClient& audio) -> void {
  // Fill one latency window, then return its credit. This distinguishes a
  // slow confirming client from the deliberate no-confirmation fallback.
  std::vector<INT16> pcm(24000uz * 2);
  auto               automatic = audio.CaptureState().auto_confirm;
  audio.CaptureState().auto_confirm = true;
  ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), 24000), 24000);
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().confirmed_frames == 24000; }));
  ASSERT_EQ(sdlrdp_audio_wait(backend.Handle(), 10000), 1);
  audio.CaptureState().auto_confirm = automatic;
  audio.CaptureState().samples.clear();
  audio.CaptureState().confirmed_frames = audio.CaptureState().maximum_pending_frames = 0;
}
auto AudioSession::ThenUnavailableAudio(Client& client, bool unmatched) -> void {
  auto events = UntilEvent(client, SDLRDP_AUDIO);
  auto event  = std::ranges::find(events, SDLRDP_AUDIO, &sdlrdp_event::type);
  ASSERT_NE(event, events.end()) << logs.Text();
  EXPECT_EQ(event->audio.connected, 0u);
  EXPECT_EQ(sdlrdp_audio_rate(backend.Handle()), 0u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_WARN, unmatched ? "rate=22050" : "client formats: none"), 1u);
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "client doesn't support any format"));
}
auto AudioSession::ThenLiveVideoAndInput(Client& client) -> void {
  FrameObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(Present(std::vector<std::uint32_t>(320uz * 200, 0x123456), 320, 200));
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty(); }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(sdlrdp_wait_frame(backend.Handle(), 10000), 1);
  ThenLiveInput(client);
}
}
