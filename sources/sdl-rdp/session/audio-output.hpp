#pragma once
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <span>

namespace Backend {
class AudioChannel;
class Configuration;
class Presenter;
class AudioOutput : private Pinned {
public:
       AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept;
  auto Open()                                  -> void;
  auto Rate()                                  -> unsigned;
  auto Wait(int timeout)                       -> int;
  auto Write(std::span<int16_t const> samples) -> int;
  auto Close()                                 -> void;

private:
  auto Channel(SessionLock const& held) const -> AudioChannel*;
  Session&             _session;
  Presenter&           _presenter;
  Configuration const& _configuration;
  bool                 _open         { };
};
}
