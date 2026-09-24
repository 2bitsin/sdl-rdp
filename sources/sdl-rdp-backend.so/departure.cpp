#include "_detail/departure.hpp"

#include "_detail/activation.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/frame-statistics.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/redirection.hpp"
#include "_detail/session-access.hpp"

#include <freerdp/settings.h>
#include <format>

namespace Backend {
Departure::Departure(PeerLink& link, SessionAccess& session, Activation& activation, Redirection& redirection,
                     FrameStatistics const& statistics, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _session{ session }, _activation{ activation }, _redirection{ redirection },
      _statistics{ statistics }, _diagnostics{ diagnostics } { }
auto Departure::Log() const -> void {
  if (!_activation.Activated()) return;
  _diagnostics.Log(SDLRDP_LOG_INFO, _statistics.Summary());
  _redirection.LogAudio();
  auto const* name = freerdp_settings_get_string(&_link.Settings(), FreeRDP_ClientHostname);
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Client {} disconnected.", name ? name : _link.Client().hostname));
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
