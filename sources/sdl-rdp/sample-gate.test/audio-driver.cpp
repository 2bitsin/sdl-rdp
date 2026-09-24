#include <sdl-rdp/sample-gate.test/audio-driver.hpp>

#include <sdl-rdp/sample-gate.test/procfs.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

#include <algorithm>
#include <vector>

namespace SampleGate {
auto AudioDriver::ThenAudioSurvivesVideoQuit(Client& client, Headless::SoundClient& audio) -> void {
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ThenPcm(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
}
auto AudioDriver::ThenRefilledLead(Client& client, Headless::SoundClient& audio, std::size_t frames) -> void {
  ASSERT_TRUE(client.Until(
      [&] { return (audio.CaptureState().samples.size() / 2) - frames >= audio.CaptureState().rate * 150 / 1000; }));
}
auto AudioDriver::ThenInitialLead(Client& client, Headless::SoundClient& audio) -> void {
  ASSERT_TRUE(
      client.Until([&] { return audio.CaptureState().samples.size() / 2 >= audio.CaptureState().rate * 140 / 1000; }));
}
auto AudioDriver::ReceiveLead() -> void {
  PlayPcm(48000uz * 5 * 2);
  if (::testing::Test::HasFatalFailure()) return;
  GivenSoundClient();
  if (::testing::Test::HasFatalFailure()) return;
  ThenInitialLead(*sound_client, *sound);
}
auto AudioDriver::GivenAudioBackend() -> void {
  ASSERT_TRUE(SetBackendHints(certificates.Path()));
}
auto AudioDriver::GivenAudioHints() -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint("SDL_RDP_PORT", "0"));
  ASSERT_TRUE(SDL_SetHint("SDL_RDP_BIND", "127.0.0.1"));
  ASSERT_TRUE(SDL_SetHint("SDL_RDP_CODEC", "planar"));
  GivenAudioBackend();
}
auto AudioDriver::GivenSoundClient() -> void {
  sound_client = std::make_unique<Client>(ListeningPort(ProcfsSelf()), true);
  sound        = std::make_unique<Headless::SoundClient>(*sound_client);
  ASSERT_TRUE(freerdp_connect(sound_client->Instance().get())) << ConnectLogs();
}
auto AudioDriver::PlayPcm(std::size_t count) -> void {
  std::vector<Sint16> pcm(count, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
}
auto AudioDriver::ConnectAudio(Client& client, Headless::SoundClient& audio) -> void {
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().ready; }));
}
auto AudioDriver::ThenPcm(Client& client, Headless::SoundClient& audio) -> void {
  PlayPcm(4800uz * 2);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.CaptureState().samples, 1234) >= 960; }));
}
auto AudioDriver::CaptureLogs() -> void {
  SDL_GetLogOutputFunction(&previous_log, &previous_log_user);
  SDL_SetLogOutputFunction(
      [](void* user, int category, SDL_LogPriority priority, char const* text) {
        auto& self  = *static_cast<AudioDriver*>(user);
        auto  level = priority >= SDL_LOG_PRIORITY_ERROR ? SDLRDP_LOG_ERROR
                      : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN
                                                          : SDLRDP_LOG_INFO;
        Headless::Logs::Collect(&self.logs, level, text);
        if (self.previous_log) self.previous_log(self.previous_log_user, category, priority, text);
      },
      this);
}
auto AudioDriver::SetUp() -> void {
  CaptureLogs();
  GivenAudioHints();
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO)) << SDL_GetError();
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  ASSERT_TRUE(stream) << SDL_GetError();
  auto port = ListeningPort(ProcfsSelf());
  ASSERT_GT(port, 0u);
}
auto AudioDriver::TearDown() -> void {
  sound.reset();
  sound_client.reset();
  stream.reset();
  SDL_Quit();
  SDL_SetLogOutputFunction(previous_log, previous_log_user);
  for (auto const* hint : { SDL_HINT_AUDIO_DRIVER, SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND",
                            "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND", "SDL_RDP_CODEC", SDL_HINT_RDP_AUDIO_LEAD })
    SDL_ResetHint(hint);
}
}
