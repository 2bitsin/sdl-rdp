#include <sdl-rdp/peer/peer.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/peer/redirection.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>

#include <format>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::link::ClientHostname;

auto Peer::Depart() -> void {
  {
    auto const held = _session.Lock();
    _redirection.Disconnect();
    LogDeparture();
  }
  _session.Depart(_link, _activation);
}
auto Peer::LogDeparture() const -> void {
  if (!_activation.Activated()) return;
  Logger().Log(LogLevel::Info, _statistics.Summary());
  _redirection.LogAudio();
  Logger().Log(LogLevel::Info, std::format("Client {} disconnected.", ClientHostname(_link)));
  Logger().Line("disconnect");
}
}
