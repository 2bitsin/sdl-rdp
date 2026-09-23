#pragma once
#include "test-backend.hpp"
#include "headless-audio.hpp"
namespace BackendGate {
using Headless::SoundClient;
class AudioGate : public RoundFive {
protected:
  void ConnectAudio(Client& client, SoundClient& audio) {
    Connect(client);
    ASSERT_TRUE(client.Until([&] { return audio.opened; }));
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& event) {
        return event.type == SDLRDP_AUDIO && event.audio.connected;
      });
    }, true, &client);
    ASSERT_TRUE(std::ranges::any_of(events, [](auto const& event) {
      return event.type == SDLRDP_AUDIO && event.audio.connected;
    })) << logs.Text();
  }
  void RunRealtimeAudio(Client& client, SoundClient& audio) {
    Expects(backend && audio.opened, "audio connection exists");
    auto writing = std::async(std::launch::async, [&] {
      std::array<INT16, 480 * 2> pcm{};
      auto start = Clock::now();
      int written = 0;
      for (unsigned tick = 1; tick <= 200; ++tick) {
        std::this_thread::sleep_until(start + std::chrono::milliseconds(tick * 10));
        auto count = sdlrdp_audio_write(backend.get(), pcm.data(), 480);
        if (count != 480) return written;
        written += count;
      }
      return written;
    });
    auto deadline = Clock::now() + std::chrono::seconds(4);
    while (audio.confirmed_frames < 96000 && Clock::now() < deadline) {
      if (!client.Pump(2)) break;
      while (!audio.pending.empty() && Clock::now() - audio.pending.front().received >= std::chrono::milliseconds(150))
        if (!audio.Confirm()) break;
    }
    sdlrdp_audio_close(backend.get());
    EXPECT_EQ(writing.get(), 96000);
    EXPECT_EQ(audio.samples.size() / 2, 96000u);
    ASSERT_GT(audio.received.size(), 1u);
    double maximum_gap = 0;
    for (std::size_t i = 1; i < audio.received.size(); ++i)
      maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.received[i] - audio.received[i - 1]).count());
    auto block_ms = 1000.0 * audio.samples.size() / 2 / audio.received.size() / audio.rate;
    RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
    EXPECT_LE(maximum_gap, 2 * block_ms + 10);
    EXPECT_EQ(audio.received.size(), 100u);
    EXPECT_FALSE(logs.Contains("Audio confirmation gate waiting"));
    EXPECT_EQ(audio.confirmed_frames, 96000u);
  }
  void CheckAudioStatistics(SoundClient const& audio) {
    Expects(!backend, "connection statistics have been flushed");
    auto text = logs.Text(true);
    std::smatch match;
    ASSERT_TRUE(std::regex_search(text, match, std::regex(
      R"(Audio: ([0-9]+) blocks sent; gap ([0-9.]+) ms mean, ([0-9.]+) ms max; ([0-9]+) gaps over 40 ms\.)"))) << text;
    EXPECT_EQ(std::stoull(match[1]), audio.received.size());
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Audio:"), 1u);
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
    EXPECT_TRUE(std::regex_search(text, std::regex(
      R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms\.)"))) << text;
  }
  void EstablishConfirmations(Client& client, SoundClient& audio) {
    // Fill one latency window, then return its credit. This distinguishes a
    // slow confirming client from the deliberate no-confirmation fallback.
    std::vector<INT16> pcm(24000 * 2);
    auto automatic = audio.auto_confirm;
    audio.auto_confirm = true;
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
    ASSERT_TRUE(client.Until([&] { return audio.confirmed_frames == 24000; }));
    ASSERT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
    audio.auto_confirm = automatic;
    audio.samples.clear();
    audio.confirmed_frames = audio.maximum_pending_frames = 0;
  }

};
}
