#pragma once
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>

#include <chrono>
#include <cstddef>

namespace BackendGate {
using Headless::SoundClient;
struct ConfirmationPace {
  std::size_t               frames;
  std::chrono::milliseconds delay;
  std::chrono::seconds      timeout;
};
class AudioSession : public RoundFive {
protected:
  static auto ConfirmDelayedAudio(Client& client, SoundClient& audio, ConfirmationPace pace) -> void;
  auto        ThenLiveInput(Client& client)                                                  -> void;
  auto        ThenRealtimeCounts(SoundClient const& audio)                                   -> void;
  auto        GivenAudioServer()                                                             -> void;
  static auto ThenAudioFormats(SoundClient const& audio)                                     -> void;
  auto        GivenUnconfirmedAudio(Client& client, SoundClient& audio)                      -> void;
  auto        ConnectAudio(Client& client, SoundClient& audio)                               -> void;
  auto        RunRealtimeAudio(Client& client, SoundClient& audio)                           -> void;
  auto        CheckAudioStatistics(SoundClient const& audio)                                 -> void;
  auto        EstablishConfirmations(Client& client, SoundClient& audio)                     -> void;
  auto        ThenUnavailableAudio(Client& client, bool unmatched)                           -> void;
  auto        ThenLiveVideoAndInput(Client& client)                                          -> void;
};
}
