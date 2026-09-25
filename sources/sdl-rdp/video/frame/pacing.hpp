#pragma once
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>
#include <sdl-rdp/video/refresh-tracker.hpp>

#include <concepts>
#include <cstdint>
#include <format>

namespace sdl_rdp::video::frame::detail::pacing {
using sdl_rdp::configuration::Configuration;
using sdl_rdp::configuration::Refresh;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;

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

namespace sdl_rdp::video::frame {
using detail::pacing::AcknowledgementMode;
using detail::pacing::FramePacing;
}
