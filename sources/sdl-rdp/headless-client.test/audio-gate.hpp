#pragma once
#include "audio-session.hpp"

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

namespace BackendGate {
auto ThenCapturedPcm(SoundClient const& audio, std::vector<std::int16_t> const& pcm) -> void;
auto ThenMissingAudioHandle()                                                        -> void;

class AudioGate : public AudioSession {
protected:
  auto        GivenConfirmingSession()                                                    -> void;
  auto        ConnectAudioFormats(Client& client, SoundClient& audio)                     -> void;
  auto        ThenInitialVolume(Client& client, SoundClient& audio)                       -> void;
  static auto ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                 std::uint32_t frames) -> void;
  auto        ThenFirstAudioBlockConfirms()                                               -> void;
  auto        WhenLastAudioBlockConfirms(std::vector<std::int16_t> const& pcm)            -> void;
  auto        WhenIdleAudioBurst(std::vector<std::int16_t> const& pcm, std::size_t burst) -> void;
  static auto ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                     std::uint32_t frames) -> void;
  auto        ThenSlowAudioConfirms(std::future<int>& writing)                            -> void;
  auto        GivenUnconfirmedSession()                                                   -> void;
  auto NewSession(std::uint32_t width = 320, std::uint32_t height = 200) -> std::pair<Client&, SoundClient&>;
  auto        UntilCaptured(std::size_t samples)                                          -> bool;
  auto        ClientSession()                                                             -> Client&;
  auto        AudioSession()                                                              -> SoundClient&;

private:
  std::unique_ptr<Client>      connected_client;
  std::unique_ptr<SoundClient> connected_audio;
};
}
