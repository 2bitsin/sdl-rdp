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
  auto Activate()                                         -> void;
  auto Deactivate() noexcept                              -> bool;
  auto Active() const noexcept                            -> bool;
  auto Activated() const noexcept                         -> bool;
  auto Finish() noexcept                                  -> void;
  auto Finished() const noexcept                          -> bool;
  auto ActivatedAt() const noexcept                       -> Clock::time_point;
  auto Hold(sdlrdp_event connection, sdlrdp_event screen) -> void;
  auto Holding() const noexcept                           -> bool;
  auto Announce(sdlrdp_codec codec, unsigned refresh_hz)  -> void;
  auto CodecChanged(sdlrdp_codec codec)                   -> void;
  auto Suppress() noexcept                                -> void;
  auto Resume() noexcept                                  -> void;
  auto Suppressed() const noexcept                        -> bool;

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
