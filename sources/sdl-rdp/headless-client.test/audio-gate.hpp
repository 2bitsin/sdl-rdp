#pragma once
#include "audio-session.hpp"

#include <future>
#include <memory>
#include <vector>

namespace BackendGate {
class AudioGate : public AudioSession {
protected:
  auto        GivenConfirmingSession()                                                 -> void;
  auto        ConnectAudioFormats(Client& client, SoundClient& audio)                  -> void;
  auto        ThenInitialVolume(Client& client, SoundClient& audio)                    -> void;
  static auto ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                 unsigned frames) -> void;
  auto        ThenFirstAudioBlockConfirms()                                            -> void;
  auto        WhenLastAudioBlockConfirms(std::vector<INT16> const& pcm)                -> void;
  auto        WhenIdleAudioBurst(std::vector<INT16> const& pcm, unsigned burst)        -> void;
  static auto ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                     unsigned frames) -> void;
  auto        ThenSlowAudioConfirms(std::future<int>& writing)                         -> void;
  static auto ThenInitialVolumeSamples(SoundClient const& audio)                       -> void;
  static auto ThenCapturedPcm(SoundClient const& audio, std::vector<INT16> const& pcm) -> void;
  static auto ThenMissingAudioHandle()                                                 -> void;
  auto        GivenUnconfirmedSession()                                                -> void;
  auto        ClientSession()                                                          -> Client&;
  auto        AudioSession()                                                           -> SoundClient&;

private:
  std::unique_ptr<Client>      connected_client;
  std::unique_ptr<SoundClient> connected_audio;
};
}
