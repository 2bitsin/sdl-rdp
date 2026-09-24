#pragma once
#include "sound-client.hpp"
#include "test-round-five.hpp"

namespace BackendGate {
using Headless::SoundClient;
class AudioSession : public RoundFive {
protected:
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
