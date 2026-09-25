#pragma once
#include <sdl-rdp/audio/forward.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/session/forward.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::session::detail::audio_output {
using sdl_rdp::audio::AudioChannel;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::link::SessionLock;
using sdl_rdp::utilities::Deadline;
using sdl_rdp::utilities::Pinned;

// An audio frame is one interleaved left and right sample.
inline constexpr std::size_t StereoChannels = 2;
class AudioOutput : private Pinned {
public:
       AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept;
  auto Open()                                       -> void;
  auto Rate()                                       -> std::uint32_t;
  auto Wait(Deadline deadline)                      -> bool;
  auto Write(std::span<std::int16_t const> samples) -> std::size_t;
  auto Close()                                      -> void;

private:
  auto Channel(SessionLock const& held) const -> std::optional<std::reference_wrapper<AudioChannel>>;
  Session&             _session;
  Presenter&           _presenter;
  Configuration const& _configuration;
  bool                 _open         { };
};
}

namespace sdl_rdp::session {
using detail::audio_output::AudioOutput;
using detail::audio_output::StereoChannels;
}
