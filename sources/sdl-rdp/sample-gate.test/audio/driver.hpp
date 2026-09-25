#pragma once
#include <sdl-rdp/sample-gate.test/audio/sample.hpp>
#include <sdl-rdp/sample-gate.test/process/captured-logs.hpp>
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>

#include <SDL3/SDL.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sdl_rdp::sample_gate_test::audio::detail::driver {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::sample_gate_test::process::AudioStream;
using sdl_rdp::sample_gate_test::process::CapturedLogs;
using sdl_rdp::sample_gate_test::process::LockedStream;
using std::chrono_literals::operator""ms;

auto ThenLead(Client& client, SoundClient& audio, std::size_t after, std::size_t milliseconds) -> void;

class AudioDriver : public AudioSample {
protected:
  auto ThenAudioSurvivesVideoQuit(Client& client, SoundClient& audio) -> void;
  auto ReceiveLead()                                                  -> void;
  auto RefillLead(auto then_refilled)                                 -> void {
    ASSERT_NO_FATAL_FAILURE(ReceiveLead());
    auto& client = *sound_client;
    auto& audio  = *sound;
    {
      LockedStream const locked{ *stream };
      ASSERT_TRUE(locked.Get().locked) << SDL_GetError();
      auto deadline = Clock::now() + 300ms;
      while (Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
    }
    auto frames  = audio.CaptureState().samples.size() / 2;
    auto resumed = Clock::now();
    ASSERT_NO_FATAL_FAILURE(ThenLead(client, audio, frames, 150));
    then_refilled(audio, resumed);
  }
  auto GivenAudioBackend()                                                      -> void;
  auto GivenAudioHints()                                                        -> void;
  auto GivenSoundClient()                                                       -> void;
  auto PlayPcm(std::size_t count)                                               -> void;
  auto PlayFlushed(std::span<std::int16_t const> pcm)                           -> Clock::time_point;
  auto QueueDrained(Clock::time_point deadline, std::chrono::milliseconds poll) -> bool;
  auto OpenStream()                                                             -> void;
  auto ConnectAudio(Client& client, SoundClient& audio)                         -> void;
  auto ThenPcm(Client& client, SoundClient& audio)                              -> void;
  auto SetUp()                                                                  -> void override;
  auto TearDown()                                                               -> void override;
  std::unique_ptr<Client>      sound_client;
  std::unique_ptr<SoundClient> sound;
  std::optional<CapturedLogs>  captured;
  AudioStream                  stream;
};
}

namespace sdl_rdp::sample_gate_test::audio {
using detail::driver::AudioDriver;
}
