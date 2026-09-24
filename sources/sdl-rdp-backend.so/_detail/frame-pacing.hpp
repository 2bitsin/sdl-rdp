#pragma once
#include "acknowledgement-window.hpp"
#include "diagnostics.hpp"
#include "frame-statistics.hpp"
#include "frame-store.hpp"
#include "pinned.hpp"
#include "refresh-tracker.hpp"

#include <concepts>
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
       FramePacing(Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
                   FrameStore& store, PeerLink& link, Activation const& activation, TraceQueue& traces,
                   FrameStatistics& statistics) noexcept;
  void Restart(FrameLock const& held);
  void Blocked();
  void Drained();
  void Sent(PeerFrames& frames, FrameCost const& cost);
  void Accept(UINT32 id);
  void Acknowledgements(AcknowledgementMode mode);
  bool Admit(std::invocable auto capacity) {
    auto const held = _store.Lock();
    if (auto const expired = _window.Expire(AcknowledgementWindow::Clock::now())) {
      _diagnostics.Line("ack-timeout", [&] { return std::format("frames={}", expired); });
      _statistics.TimedOut(expired);
      _store.Notify();
    }
    return !_window.Enabled() || _window.Open(capacity());
  }
  DWORD    Timeout();
  unsigned Effective() const noexcept;
  UINT32   Frame() const     noexcept;
  void     Begin()           noexcept;
  bool     Settled(FrameLock const& held, uint64_t target) const;
  uint64_t Acknowledged(FrameLock const& held) const;

private:
  void Adjust(std::invocable<Refresh&> auto step);
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
