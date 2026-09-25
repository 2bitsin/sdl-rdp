#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::peer::detail::departure {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::frame::FrameStatistics;

class Departure : private Pinned {
public:
       Departure(PeerLink& link, SessionAccess& session, Activation& activation, Redirection& redirection,
                 FrameStatistics const& statistics, Diagnostics const& diagnostics) noexcept;
  auto Depart() -> void;

private:
  auto Log() const -> void;
  PeerLink&              _link;
  SessionAccess&         _session;
  Activation&            _activation;
  Redirection&           _redirection;
  FrameStatistics const& _statistics;
  Diagnostics const&     _diagnostics;
};
}

namespace sdl_rdp::peer {
using detail::departure::Departure;
}
