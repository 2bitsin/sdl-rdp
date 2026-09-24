#include "_detail/departure.hpp"

#include "_detail/activation.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/frame-statistics.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/redirection.hpp"
#include "_detail/session-access.hpp"

#include <format>
#include <freerdp/settings.h>

namespace Backend {
Departure::Departure(PeerLink& link, SessionAccess& session, Activation& activation, Redirection& redirection,
                     FrameStatistics const& statistics, Diagnostics const& diagnostics) noexcept
    : _link { link }, _session{ session }, _activation{ activation }, _redirection{ redirection },
      _statistics{ statistics }, _diagnostics{ diagnostics } { }
void Departure::Log() const {
  if (!_activation.Activated()) return;
  _diagnostics.Log(SDLRDP_LOG_INFO, _statistics.Summary());
  _redirection.LogAudio();
  auto const* name = freerdp_settings_get_string(&_link.Settings(), FreeRDP_ClientHostname);
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Client {} disconnected.", name ? name : _link.Client().hostname));
  _diagnostics.Line("disconnect");
}
void Departure::Depart() {
  {
    auto const held = _session.Lock();
    _redirection.Disconnect();
    Log();
  }
  _session.Depart(_link, _activation);
}
}
