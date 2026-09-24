#pragma once
#include "sdl-rdp-backend.h"

#include <atomic>
#include <chrono>
#include <optional>

namespace Backend {
class EventQueue;
class PeerLink;
class Activation {
public:
  using Clock = std::chrono::steady_clock;
                    Activation(EventQueue& events, PeerLink& link) noexcept;
  void              Activate();
  bool              Deactivate()                                   noexcept;
  bool              Active() const                                 noexcept;
  bool              Activated() const                              noexcept;
  void              Finish()                                       noexcept;
  bool              Finished() const                               noexcept;
  Clock::time_point ActivatedAt() const                            noexcept;
  void              Hold(sdlrdp_event connection, sdlrdp_event screen);
  bool              Holding() const                                noexcept;
  void              Announce(sdlrdp_codec codec, unsigned refresh_hz);
  void              CodecChanged(sdlrdp_codec codec);
  void              Suppress()                                     noexcept;
  void              Resume()                                       noexcept;
  bool              Suppressed() const                             noexcept;

private:
  EventQueue&                 _events;
  PeerLink&                   _link;
  std::optional<sdlrdp_event> _connection;
  sdlrdp_event                _screen      { };
  Clock::time_point           _activated_at;
  // Atomics are read by the app thread; the rest belong to the peer thread under the session lock.
  std::atomic_bool            _active;
  std::atomic_bool            _finished;
  bool                        _activated   { };
  bool                        _suppressed  { };
};
}
