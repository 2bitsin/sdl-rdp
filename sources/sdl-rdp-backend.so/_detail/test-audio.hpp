#pragma once
#include "headless-audio.hpp"
#include "test-backend.hpp"

#include <cstddef>
namespace BackendGate {
using Headless::SoundClient;
inline int WriteRealtimeAudio(sdlrdp_handle* backend) {
  std::array<INT16, 480uz * 2> pcm    { };
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
inline void ConfirmDelayedAudio(Client& client, SoundClient& audio) {
  auto deadline = Clock::now() + std::chrono::seconds(4);
  while (audio.CaptureState().confirmed_frames < 96000 && Clock::now() < deadline) {
    if (!client.Pump(2)) break;
    while (!audio.CaptureState().pending.empty() &&
           Clock::now() - audio.CaptureState().pending.front().received >= std::chrono::milliseconds(150))
      if (!audio.Confirm()) break;
  }
}
inline void ThenAudioCadence(SoundClient const& audio) {
  ASSERT_GT(audio.CaptureState().received.size(), 1u);
  double maximum_gap = 0;
  for (std::size_t i = 1; i < audio.CaptureState().received.size(); ++i)
    maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.CaptureState().received[i] -
                                                                                  audio.CaptureState().received[i - 1])
                                            .count());
  auto block_ms = 1000.0 * double(audio.CaptureState().samples.size()) / 2 /
                  double(audio.CaptureState().received.size()) / audio.CaptureState().rate;
  testing::Test::RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
  EXPECT_LE(maximum_gap, (2 * block_ms) + 10);
}
class AudioSession : public RoundFive {
protected:
  void ThenLiveInput(Client& client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
    auto events = EventsUntil(
        [](auto const& events) {
          return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_KEY; });
        },
        true, &client);
    EXPECT_NE(std::ranges::find(events, SDLRDP_KEY, &sdlrdp_event::type), events.end());
    EXPECT_EQ(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  }
  void ThenRealtimeCounts(SoundClient const& audio) {
    EXPECT_EQ(audio.CaptureState().received.size(), 100u);
    EXPECT_FALSE(logs.Contains("Audio confirmation gate waiting"));
    EXPECT_EQ(audio.CaptureState().confirmed_frames, 96000u);
  }
  void GivenAudioServer() {
    Open(320, 200);
    ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  }
  static void ThenAudioFormats(SoundClient const& audio) {
    ASSERT_EQ(audio.CaptureState().server_formats.size(), 2u);
    EXPECT_EQ(audio.CaptureState().server_formats[0].nSamplesPerSec, 44100u);
    EXPECT_EQ(audio.CaptureState().server_formats[1].nSamplesPerSec, 48000u);
  }
  void GivenUnconfirmedAudio(Client& client, SoundClient& audio) {
    audio.CaptureState().rate         = 48000;
    audio.CaptureState().auto_confirm = false;
    ConnectAudio(client, audio);
  }
  void ConnectAudio(Client& client, SoundClient& audio) {
    Connect(client);
    ASSERT_TRUE(client.Until([&] { return audio.CaptureState().opened; }));
    auto events = EventsUntil(
        [](auto const& events) {
          return std::ranges::any_of(
              events, [](auto const& event) { return event.type == SDLRDP_AUDIO && event.audio.connected; });
        },
        true, &client);
    ASSERT_TRUE(std::ranges::any_of(events, [](auto const& event) {
      return event.type == SDLRDP_AUDIO && event.audio.connected;
    })) << logs.Text();
  }
  void RunRealtimeAudio(Client& client, SoundClient& audio) {
    Expects(backend != nullptr, "backend exists");
    Expects(audio.CaptureState().opened, "client audio channel is open");
    auto writing = std::async(std::launch::async, [&] { return WriteRealtimeAudio(backend.get()); });
    ConfirmDelayedAudio(client, audio);
    sdlrdp_audio_close(backend.get());
    EXPECT_EQ(writing.get(), 96000);
    EXPECT_EQ(audio.CaptureState().samples.size() / 2, 96000u);
    ThenAudioCadence(audio);
    if (::testing::Test::HasFatalFailure()) return;
    ThenRealtimeCounts(audio);
  }
  void CheckAudioStatistics(SoundClient const& audio) {
    Expects(!backend, "connection statistics have been flushed");
    auto        text  = logs.Text(true);
    std::smatch match;
    ASSERT_TRUE(std::regex_search(
        text, match,
        std::regex(
            R"(Audio: ([0-9]+) blocks sent; gap ([0-9.]+) ms mean, ([0-9.]+) ms max; ([0-9]+) gaps over 40 ms\.)")))
        << text;
    EXPECT_EQ(std::stoull(match[1]), audio.CaptureState().received.size());
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Audio:"), 1u);
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
    EXPECT_TRUE(std::regex_search(
        text, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms, [0-9]+ timed out\.)")))
        << text;
  }
  void EstablishConfirmations(Client& client, SoundClient& audio) {
    // Fill one latency window, then return its credit. This distinguishes a
    // slow confirming client from the deliberate no-confirmation fallback.
    std::vector<INT16> pcm(24000uz * 2);
    auto automatic = audio.CaptureState().auto_confirm;
    audio.CaptureState().auto_confirm = true;
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
    ASSERT_TRUE(client.Until([&] { return audio.CaptureState().confirmed_frames == 24000; }));
    ASSERT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
    audio.CaptureState().auto_confirm = automatic;
    audio.CaptureState().samples.clear();
    audio.CaptureState().confirmed_frames = audio.CaptureState().maximum_pending_frames = 0;
  }
  void ThenUnavailableAudio(Client& client, bool unmatched) {
    auto events = EventsUntil(
        [](auto const& events) {
          return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_AUDIO; });
        },
        true, &client);
    auto event = std::ranges::find(events, SDLRDP_AUDIO, &sdlrdp_event::type);
    ASSERT_NE(event, events.end()) << logs.Text();
    EXPECT_EQ(event->audio.connected, 0u);
    EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
    {

      EXPECT_EQ(std::ranges::count_if(logs.Entries(),
                                      [&](auto const& line) {
                                        return line.first == SDLRDP_LOG_WARN &&
                                               line.second.contains(unmatched ? "rate=22050" : "client formats: none");
                                      }),
                1);
    }
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "client doesn't support any format"));
  }
  void ThenLiveVideoAndInput(Client& client) {
    FrameObserver observer(client);
    Present(std::vector<UINT32>(320uz * 200, 0x123456), 320, 200);
    ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty(); }));
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    ThenLiveInput(client);
  }
};
class AudioGate : public AudioSession {
protected:
  void GivenConfirmingSession() {
    GivenUnconfirmedSession();
    if (::testing::Test::HasFatalFailure()) return;
    EstablishConfirmations(ClientSession(), AudioSession());
  }
  void ConnectAudioFormats(Client& client, SoundClient& audio) {
    ConnectAudio(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
    ThenAudioFormats(audio);
  }
  void ThenInitialVolume(Client& client, SoundClient& audio) {
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
  static void ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                 unsigned frames) {
    EXPECT_TRUE(freerdp_disconnect(client.Instance().get()));
    EXPECT_EQ(writing.get(), frames);
    if (reconnect) EXPECT_EQ(audio.CaptureState().samples.size(), 1920u);
  }
  void ThenFirstAudioBlockConfirms() {
    ASSERT_TRUE(AudioSession().Confirm());
    EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);

    EXPECT_EQ(std::ranges::count_if(logs.Entries(),
                                    [](auto const& line) {
                                      return line.first == SDLRDP_LOG_WARN &&
                                             line.second.contains("Audio confirmation gate waiting");
                                    }),
              1);
  }
  void WhenLastAudioBlockConfirms(std::vector<INT16> const& pcm) {
    ASSERT_TRUE(AudioSession().Confirm(24));
    EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
    ASSERT_TRUE(ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == 49920; }));
    EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  }
  void WhenIdleAudioBurst(std::vector<INT16> const& pcm, unsigned burst) {
    auto started = Clock::now();
    auto writing = std::async(std::launch::async, [&] { return sdlrdp_audio_write(backend.get(), pcm.data(), 48000); });
    auto captured =
        ClientSession().Until([&] { return AudioSession().CaptureState().samples.size() == burst * pcm.size(); });
    if (!captured) sdlrdp_audio_close(backend.get());
    EXPECT_TRUE(captured);
    EXPECT_EQ(writing.get(), 48000);
    EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
    if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  static void ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                     unsigned frames) {
    auto received = client.Until([&] { return audio.CaptureState().samples.size() >= (reconnect ? 1920u : 48000u); });
    EXPECT_TRUE(received);
    if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ThenWriterFinishes(client, audio, writing, reconnect, frames);
  }
  void ThenSlowAudioConfirms(std::future<int>& writing) {
    if (AudioSession().CaptureState().confirmed_frames < 480000) sdlrdp_audio_close(backend.get());
    EXPECT_EQ(AudioSession().CaptureState().confirmed_frames, 480000u);
    EXPECT_LE(AudioSession().CaptureState().maximum_pending_frames, 24960u);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
    RecordProperty("audio_diagnostics", logs.Text(true));
    EXPECT_EQ(writing.get(), 480000);
    RecordProperty("maximum_unconfirmed_ms",
                   std::to_string(double(AudioSession().CaptureState().maximum_pending_frames) / 48.0));
  }
  static void ThenInitialVolumeSamples(SoundClient const& audio) {
    for (auto frame : audio.CaptureState().samples | std::views::chunk(2)) {
      EXPECT_EQ(frame[0], -12000);
      EXPECT_EQ(frame[1], 6000);
    }
  }
  static void ThenCapturedPcm(SoundClient const& audio, std::vector<INT16> const& pcm) {
    EXPECT_EQ(audio.CaptureState().samples.size(), pcm.size());
    EXPECT_EQ(audio.CaptureState().samples.front(), pcm.front());
    EXPECT_EQ(audio.CaptureState().samples.back(), pcm.back());
    EXPECT_TRUE(std::ranges::equal(audio.CaptureState().samples, pcm));
    EXPECT_EQ(audio.CaptureState().pending.size(), 0u);
  }
  static void ThenMissingAudioHandle() {
    sdlrdp_audio_close(nullptr);
    EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
    EXPECT_STREQ(sdlrdp_last_error(), "Invalid audio handle.");
    EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
  }
  void GivenUnconfirmedSession() {
    GivenAudioServer();
    if (::testing::Test::HasFatalFailure()) return;
    connected_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
    connected_audio  = std::make_unique<SoundClient>(*connected_client);
    GivenUnconfirmedAudio(*connected_client, *connected_audio);
  }
  Client& ClientSession() { return *connected_client; }
  SoundClient& AudioSession() { return *connected_audio; }

private:
  std::unique_ptr<Client>      connected_client;
  std::unique_ptr<SoundClient> connected_audio;
};

}
