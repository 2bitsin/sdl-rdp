#include <sdl-rdp/peer/departure.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/peer/redirection.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>

#include <format>

namespace sdl_rdp::peer::detail::departure {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::link::ClientHostname;

Departure::Departure(PeerLink& link, SessionAccess& session, Activation& activation, Redirection& redirection,
                     FrameStatistics const& statistics, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _session{ session }, _activation{ activation }, _redirection{ redirection },
      _statistics{ statistics }, _diagnostics{ diagnostics } { }
auto Departure::Log() const -> void {
  if (!_activation.Activated()) return;
  _diagnostics.Log(LogLevel::Info, _statistics.Summary());
  _redirection.LogAudio();
  _diagnostics.Log(LogLevel::Info, std::format("Client {} disconnected.", ClientHostname(_link)));
  _diagnostics.Line("disconnect");
}
auto Departure::Depart() -> void {
  {
    auto const held = _session.Lock();
    _redirection.Disconnect();
    Log();
  }
  _session.Depart(_link, _activation);
}
}
