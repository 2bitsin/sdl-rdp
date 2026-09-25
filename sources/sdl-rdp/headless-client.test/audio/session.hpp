#pragma once
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/session/backend.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::audio::detail::session {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::session::Backend;
// Writes `frames` stereo frames of `pcm` starting at frame `first`; the count the backend took.
auto WriteFrames(Backend& backend, std::span<std::int16_t const> pcm, std::size_t first, std::size_t frames)
    -> std::size_t;
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

namespace sdl_rdp::headless_client_test::audio {
using detail::session::AudioSession;
using detail::session::ConfirmationPace;
using detail::session::WriteFrames;
}
