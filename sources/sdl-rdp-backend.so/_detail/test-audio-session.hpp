#pragma once
#include "sound-client.hpp"
#include "test-round-five.hpp"

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
  void        ThenLiveInput(Client& client);
  void        ThenRealtimeCounts(SoundClient const& audio);
  void        GivenAudioServer();
  static void ThenAudioFormats(SoundClient const& audio);
  void        GivenUnconfirmedAudio(Client& client, SoundClient& audio);
  void        ConnectAudio(Client& client, SoundClient& audio);
  void        RunRealtimeAudio(Client& client, SoundClient& audio);
  void        CheckAudioStatistics(SoundClient const& audio);
  void        EstablishConfirmations(Client& client, SoundClient& audio);
  void        ThenUnavailableAudio(Client& client, bool unmatched);
  void        ThenLiveVideoAndInput(Client& client);
};
}
