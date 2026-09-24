#pragma once
#include "pinned.hpp"
#include "session.hpp"

#include <cstdint>
#include <span>

namespace Backend {
class AudioChannel;
class Configuration;
class Presenter;
class AudioOutput : private Pinned {
public:
           AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept;
  void     Open();
  unsigned Rate();
  int      Wait(int timeout);
  int      Write(std::span<int16_t const> samples);
  void     Close();

private:
  AudioChannel* Channel(SessionLock const& held) const;
  Session&             _session;
  Presenter&           _presenter;
  Configuration const& _configuration;
  bool                 _open         { };
};
}
