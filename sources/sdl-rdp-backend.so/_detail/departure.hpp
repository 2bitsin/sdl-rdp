#pragma once
#include "pinned.hpp"

namespace Backend {
class Activation;
class Diagnostics;
class FrameStatistics;
class PeerLink;
class Redirection;
class SessionAccess;
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
