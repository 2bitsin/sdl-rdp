#pragma once
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Backend {
// The ABI's audio frame is one interleaved left and right sample.
inline constexpr std::size_t StereoChannels = 2;
class AudioChannel;
class Configuration;
class Presenter;
class AudioOutput : private Pinned {
public:
       AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept;
  auto Open()                                       -> void;
  auto Rate()                                       -> std::uint32_t;
  auto Wait(Deadline deadline)                      -> int;
  auto Write(std::span<std::int16_t const> samples) -> int;
  auto Close()                                      -> void;

private:
  auto Channel(SessionLock const& held) const -> std::optional<std::reference_wrapper<AudioChannel>>;
  Session&             _session;
  Presenter&           _presenter;
  Configuration const& _configuration;
  bool                 _open         { };
};
}
