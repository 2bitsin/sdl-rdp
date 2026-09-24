#pragma once
#include <sdl-rdp/sample-gate.test/audio-sample.hpp>

#include <SDL3/SDL.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>

namespace SampleGate {
auto ThenLead(Client& client, Headless::SoundClient& audio, std::size_t after, std::size_t milliseconds) -> void;

class AudioDriver : public AudioSample {
protected:
  auto ThenAudioSurvivesVideoQuit(Client& client, Headless::SoundClient& audio) -> void;
  auto ReceiveLead()                                                            -> void;
  auto RefillLead(auto then_refilled)                                           -> void {
    ASSERT_NO_FATAL_FAILURE(ReceiveLead());
    auto& client = *sound_client;
    auto& audio  = *sound;
    {
      ASSERT_TRUE(SDL_LockAudioStream(stream.get()));
      std::unique_ptr<SDL_AudioStream, decltype(&SDL_UnlockAudioStream)> const locked(stream.get(),
                                                                                      SDL_UnlockAudioStream);
      auto deadline = Clock::now() + 300ms;
      while (Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
    }
    auto frames  = audio.CaptureState().samples.size() / 2;
    auto resumed = Clock::now();
    ASSERT_NO_FATAL_FAILURE(ThenLead(client, audio, frames, 150));
    then_refilled(audio, resumed);
  }
  auto GivenAudioBackend()                                        -> void;
  auto GivenAudioHints()                                          -> void;
  auto GivenSoundClient()                                         -> void;
  auto PlayPcm(std::size_t count)                                 -> void;
  auto PlayFlushed(std::span<Sint16 const> pcm)                   -> Clock::time_point;
  auto OpenStream()                                               -> void;
  auto ConnectAudio(Client& client, Headless::SoundClient& audio) -> void;
  auto ThenPcm(Client& client, Headless::SoundClient& audio)      -> void;
  auto CaptureLogs()                                              -> void;
  auto SetUp()                                                    -> void override;
  auto TearDown()                                                 -> void override;
  std::unique_ptr<Client>                sound_client;
  std::unique_ptr<Headless::SoundClient> sound;
  SDL_LogOutputFunction                  previous_log      = nullptr;
  void*                                  previous_log_user = nullptr;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{ nullptr, SDL_DestroyAudioStream };
};
}
