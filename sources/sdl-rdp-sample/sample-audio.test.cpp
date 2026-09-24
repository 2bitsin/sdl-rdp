#include <sdl-rdp/sample-gate.test/audio-driver.hpp>
#include <sdl-rdp/sample-gate.test/procfs.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <sdl-rdp/headless-client.test/tone-measurements.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace SampleGate {
namespace {
auto ThenDeviceTone(Headless::SoundClient const& audio, std::string const& line) -> void {
  auto [frequency, db] = Headless::ToneMeasurements(audio.CaptureState().samples, audio.CaptureState().rate);
  EXPECT_NEAR(frequency, 440, 8.8);
  EXPECT_NEAR(db, -12, 0.3);
  testing::Test::RecordProperty("device_format", line);
  testing::Test::RecordProperty("tone_hz", std::to_string(frequency));
  testing::Test::RecordProperty("tone_dbfs", std::to_string(db));
}
auto ThenTone(Headless::SoundClient const& audio, Headless::FrameObserver const& frames, bool tight) -> void {
  ThenDeviceTone(audio, tight ? "tight vsync" : "default vsync");
  if (tight) EXPECT_GE(frames.Frames().size(), 2u);
}
auto ToneCaptured(Headless::SoundClient const& audio, Headless::FrameObserver const& frames) -> bool {
  return audio.CaptureState().samples.size() >= std::size_t(audio.CaptureState().rate) * 2
         && frames.Frames().size() >= 2;
}
}

namespace {
TEST_F(AudioSample, ToneAndVsync) {
  WhenTonePlayedTwice(ToneCaptured, ThenTone);
}

TEST_F(AudioSample, ToneAtClientRate) {
  WhenTonePlayed(false, 48000, ToneCaptured, [this](auto const& audio, auto const& /*frames*/, bool /*tight*/) {
    ASSERT_TRUE(Read("audio device=RDP client freq=48000"));
    ThenDeviceTone(audio, line);
  });
}

TEST_F(AudioDriver, ClientReceivesOneLeadOnAttach) {
  ReceiveLead();
}
TEST_F(AudioDriver, StallRefillsTheLead) {
  RefillLead([](Headless::SoundClient const& /*audio*/, Clock::time_point /*resumed*/) { });
}
TEST_F(AudioDriver, LeadAtOrAboveLatencyFailsOpen) {
  stream.reset();
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  for (auto const* lead : { "500", "501" }) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, lead));
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    EXPECT_FALSE(stream);
    EXPECT_STREQ(SDL_GetError(), "RDP audio lead must be below the audio latency window");
  }
}
TEST_F(AudioDriver, AudioBeforeVideoSurvivesVideoQuit) {
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0);
  ASSERT_GT(port, 0);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_NO_FATAL_FAILURE(ThenAudioSurvivesVideoQuit(client, audio));
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  EXPECT_EQ(SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0), port);
}

TEST_F(AudioDriver, AudioOnlyPlaysBlackDesktop) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  auto port = ListeningPort(ProcfsSelf());
  ASSERT_GT(port, 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  auto*               gdi   = client.Instance()->context->gdi;
  std::vector<UINT32> black(std::size_t(gdi->width) * gdi->height);
  ASSERT_TRUE(client.Until([&] { return client.Matches(black); }));
  ASSERT_NO_FATAL_FAILURE(ThenPcm(client, audio));
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
}
}
}
