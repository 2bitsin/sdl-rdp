#include <sdl-rdp/sample-gate.test/audio/driver.hpp>

#include <sdl-rdp/sample-gate.test/process/procfs.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/utilities/deadline.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdl_rdp::sample_gate_test::audio::detail::driver {
using sdl_rdp::sample_gate_test::process::Capture;
using sdl_rdp::sample_gate_test::process::ListeningPort;
using sdl_rdp::sample_gate_test::process::ProcfsSelf;
using sdl_rdp::sample_gate_test::sample::SetCertificateHint;
using sdl_rdp::utilities::Sleeping;
using sdl_rdp::utilities::Until;

auto ThenLead(Client& client, SoundClient& audio, std::size_t after, std::size_t milliseconds) -> void {
  ASSERT_TRUE(client.Until([&] {
    return (audio.CaptureState().samples.size() / 2) - after >= audio.CaptureState().rate * milliseconds / 1000;
  }));
}
auto AudioDriver::ThenAudioSurvivesVideoQuit(Client& client, SoundClient& audio) -> void {
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_NO_FATAL_FAILURE(ThenPcm(client, audio));
}
auto AudioDriver::ReceiveLead() -> void {
  ASSERT_NO_FATAL_FAILURE(PlayPcm(48000uz * 5 * 2));
  ASSERT_NO_FATAL_FAILURE(GivenSoundClient());
  ThenLead(*sound_client, *sound, 0, 140);
}
auto AudioDriver::GivenAudioBackend() -> void {
  ASSERT_TRUE(SetCertificateHint(certificates.Path()));
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
  sound        = std::make_unique<SoundClient>(*sound_client);
  ASSERT_NO_FATAL_FAILURE(Connect(*sound_client));
}
auto AudioDriver::PlayPcm(std::size_t count) -> void {
  std::vector<std::int16_t> pcm(count, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(std::int16_t)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
}
auto AudioDriver::PlayFlushed(std::span<std::int16_t const> pcm) -> Clock::time_point {
  EXPECT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), static_cast<int>(pcm.size_bytes())));
  EXPECT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto const started = Clock::now();
  EXPECT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  return started;
}
auto AudioDriver::QueueDrained(Clock::time_point deadline, std::chrono::milliseconds poll) -> bool {
  return Until(deadline, Sleeping(poll), [this] { return SDL_GetAudioStreamQueued(stream.get()) == 0; });
}
auto AudioDriver::OpenStream() -> void {
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  ASSERT_TRUE(stream) << SDL_GetError();
}
auto AudioDriver::ConnectAudio(Client& client, SoundClient& audio) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().ready; }));
}
auto AudioDriver::ThenPcm(Client& client, SoundClient& audio) -> void {
  ASSERT_NO_FATAL_FAILURE(PlayPcm(4800uz * 2));
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.CaptureState().samples, 1234) >= 960; }));
}
auto AudioDriver::SetUp() -> void {
  captured.emplace(logs, Capture{ .forwarded = true });
  ASSERT_NO_FATAL_FAILURE(GivenAudioHints());
  ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO)) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(OpenStream());
  auto port = ListeningPort(ProcfsSelf());
  ASSERT_GT(port, 0u);
}
auto AudioDriver::TearDown() -> void {
  sound.reset();
  sound_client.reset();
  stream.reset();
  SDL_Quit();
  captured.reset();
  for (auto const* hint : { SDL_HINT_AUDIO_DRIVER, SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND",
                            "SDL_RDP_CERT_DIR", "SDL_RDP_CODEC", SDL_HINT_RDP_AUDIO_LEAD })
    SDL_ResetHint(hint);
}
}
