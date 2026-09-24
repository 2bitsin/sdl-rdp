#pragma once
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/frame-store.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/frame-statistics.hpp>
#include <sdl-rdp/video/refresh-tracker.hpp>

#include <concepts>
#include <cstdint>
#include <format>

namespace Backend {
class Activation;
class Configuration;
class EventQueue;
class PeerFrames;
class PeerLink;
class TraceQueue;
enum class AcknowledgementMode{ Suspended, Tracking, Restarted };
class FramePacing : private Pinned {
public:
  FramePacing(Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration, FrameStore& store,
              PeerLink& link, Activation const& activation, TraceQueue& traces, FrameStatistics& statistics) noexcept;
  auto Restart(FrameLock const& held)                  -> void;
  auto Blocked()                                       -> void;
  auto Drained()                                       -> void;
  auto Sent(PeerFrames& frames, FrameCost const& cost) -> void;
  auto Accept(std::uint32_t id)                        -> void;
  auto Acknowledgements(AcknowledgementMode mode)      -> void;
  auto Admit(std::invocable auto capacity)             -> bool {
    auto const held = _store.Lock();
    if (auto const expired = _window.Expire(AcknowledgementWindow::Clock::now())) {
      _diagnostics.Line("ack-timeout", [&] { return std::format("frames={}", expired); });
      _statistics.TimedOut(expired);
      _store.Notify();
    }
    return !_window.Enabled() || _window.Open(capacity());
  }
  auto Timeout()                                                  -> std::uint32_t;
  auto Effective() const noexcept                                 -> std::uint32_t;
  auto Frame() const noexcept                                     -> std::uint32_t;
  auto Begin() noexcept                                           -> void;
  auto Settled(FrameLock const& held, std::uint64_t target) const -> bool;
  auto Acknowledged(FrameLock const& held) const                  -> std::uint64_t;

private:
  auto Adjust(std::invocable<Refresh&> auto step) -> void;
  Diagnostics const&    _diagnostics;
  EventQueue&           _events;
  Configuration const&  _configuration;
  FrameStore&           _store;
  PeerLink&             _link;
  Activation const&     _activation;
  TraceQueue&           _traces;
  FrameStatistics&      _statistics;
  AcknowledgementWindow _window;
  RefreshTracker        _refresh;
};
}
