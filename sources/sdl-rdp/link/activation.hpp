#pragma once
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/forward.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace sdl_rdp::link::detail::activation {
using sdl_rdp::configuration::Codec;

class Activation {
public:
  using Clock = std::chrono::steady_clock;
       Activation(EventQueue& events, PeerLink& link) noexcept;
  auto Activate()                                       -> void;
  auto Deactivate() noexcept                            -> bool;
  auto Active() const noexcept                          -> bool;
  auto Activated() const noexcept                       -> bool;
  auto Finish() noexcept                                -> void;
  auto Finished() const noexcept                        -> bool;
  auto ActivatedAt() const noexcept                     -> Clock::time_point;
  auto Hold(Connected connection, ScreenChanged screen) -> void;
  auto Holding() const noexcept                         -> bool;
  auto Announce(Codec codec, std::uint32_t refresh_hz)  -> void;
  auto CodecChanged(Codec codec)                        -> void;
  auto Suppress() noexcept                              -> void;
  auto Resume() noexcept                                -> void;
  auto Suppressed() const noexcept                      -> bool;

private:
  EventQueue&              _events;
  PeerLink&                _link;
  std::optional<Connected> _connection;
  ScreenChanged            _screen;
  Clock::time_point        _activated_at;
  // Atomics are read by the app thread; the rest belong to the peer thread under the session lock.
  std::atomic_bool _active;
  std::atomic_bool _finished;
  bool             _activated { };
  bool             _suppressed{ };
};
}

namespace sdl_rdp::link {
using detail::activation::Activation;
}
