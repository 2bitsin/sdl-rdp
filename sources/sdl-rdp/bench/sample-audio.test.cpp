#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <sdl-rdp/sample-gate.test/audio-driver.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ranges>
#include <string>
#include <vector>

namespace SampleGate {
namespace {
auto ThenBlockCadence(Headless::SoundClient const& audio, bool tight) -> void {
  std::ranges::for_each(std::views::iota(0, 3), [&](int second) {
    auto start  = audio.CaptureState().received.front() + std::chrono::seconds(second);
    auto blocks = std::ranges::count_if(audio.CaptureState().received,
                                        [&](auto time) { return time >= start && time < start + 1s; });
    EXPECT_GE(blocks, 45) << "tight=" << tight << " second=" << second;
    SDL_Log("tone tight=%d second=%d blocks=%zu", tight, second, std::size_t(blocks));
  });
}
auto ThreeSecondsCaptured(Headless::SoundClient const& audio, Headless::FrameObserver const& /*frames*/) -> bool {
  return !audio.CaptureState().received.empty() && Clock::now() >= audio.CaptureState().received.front() + 3s;
}
auto ThenLeadCadence(Headless::SoundClient const& audio, size_t first, size_t frames) -> void {
  ASSERT_GT(audio.CaptureState().received.size(), first);
  double maximum_gap = 0;
  for (auto i = first; i < audio.CaptureState().received.size(); ++i)
    maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(
                                            audio.CaptureState().received[i] - audio.CaptureState().received[i - 1])
                                            .count());
  auto sent_frames = (audio.CaptureState().samples.size() / 2) - frames;
  auto block_ms    = 1000.0 * double(sent_frames) / double(audio.CaptureState().received.size() - first)
                     / audio.CaptureState().rate;
  EXPECT_LE(maximum_gap, (2 * block_ms) + 10);
  auto elapsed = std::chrono::duration<double>(audio.CaptureState().received.back()
                                               - audio.CaptureState().received[first - 1])
                     .count();
  EXPECT_NEAR(double(sent_frames) / audio.CaptureState().rate, elapsed, 0.030);
  testing::Test::RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
}
TEST_F(AudioSample, BlockCadence) {
  WhenTonePlayedTwice(ThreeSecondsCaptured,
                      [](auto const& audio, auto const& /*frames*/, bool tight) { ThenBlockCadence(audio, tight); });
}
TEST_F(AudioDriver, NoClientTenSecondClock) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  EXPECT_STREQ(SDL_GetCurrentAudioDriver(), "rdp");
  std::vector<Sint16> frames(480000uz * 2, 1000);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), frames.data(), frames.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  auto deadline = started + 30s;
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < deadline) SDL_Delay(5);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  // Consuming ten seconds of PCM may run one lead ahead of real time.
  // SDL may dequeue one buffer ahead; scheduling delays only make this longer.
  int           buffer_frames = 0;
  SDL_AudioSpec format        { };
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &format, &buffer_frames));
  EXPECT_GE(elapsed, 10.0 - 0.150 - (double(buffer_frames) / format.freq));
  RecordProperty("no_client_ten_seconds_elapsed", std::to_string(elapsed));
}
TEST_F(AudioDriver, InitialLeadClock) {
  ReceiveLead();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_LE(sound->CaptureState().received.back(), sound->CaptureState().received.front() + 100ms);
}
TEST_F(AudioDriver, LeadCadence) {
  ReceiveLead();
  if (::testing::Test::HasFatalFailure()) return;
  auto first    = sound->CaptureState().received.size();
  auto frames   = sound->CaptureState().samples.size() / 2;
  auto deadline = Clock::now() + 1s;
  while (Clock::now() < deadline) ASSERT_TRUE(sound_client->Pump(1));
  ThenLeadCadence(*sound, first, frames);
}
TEST_F(AudioDriver, StallRefillClock) {
  RefillLead([](Headless::SoundClient const& audio, Clock::time_point resumed) {
    EXPECT_LE(audio.CaptureState().received.back(), resumed + 100ms);
  });
}
TEST_F(AudioDriver, ZeroLeadKeepsRealtimeClock) {
  stream.reset();
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, "0"));
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  ASSERT_TRUE(stream) << SDL_GetError();
  std::vector<Sint16> pcm(48000uz * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < started + 3s) SDL_Delay(1);
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  EXPECT_GE(Clock::now() - started, 990ms);
}
}
}
