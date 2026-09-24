#include "_detail/transport-end.hpp"

#include "_detail/activation.hpp"
#include "_detail/auth.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/logging.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"

#include <array>
#include <format>
#include <ranges>
#include <freerdp/settings.h>
#include <string>
#include <utility>

namespace Backend {
namespace {
constexpr std::array<std::pair<UINT32, char const*>, 5> ProtocolFlags{ { { SecurityTls   , "TLS"     },
                                                                         { SecurityNla   , "NLA"     },
                                                                         { SecurityNlaExt, "NLA_EXT" },
                                                                         { SecurityRdstls, "RDSTLS"  },
                                                                         { SecurityRdsaad, "RDSAAD"  } } };
std::string ProtocolNames(UINT32 mask, bool rdp) {
  std::string names = rdp ? "RDP" : "";
  for (auto const& entry : ProtocolFlags | std::views::filter([mask](auto entry) { return mask & entry.first; })) {
    if (!names.empty()) names += '|';
    names += entry.second;
  }
  return names;
}
std::string Refusal(rdpSettings const& settings, std::string const& protocols) {
  auto const offered = (freerdp_settings_get_bool(&settings, FreeRDP_TlsSecurity) ? SecurityTls : 0) |
                       (freerdp_settings_get_bool(&settings, FreeRDP_NlaSecurity) ? SecurityNla : 0);
  return std::format("Connection refused: client requested {}, server offers {}", protocols,
                     ProtocolNames(offered, freerdp_settings_get_bool(&settings, FreeRDP_RdpSecurity)));
}
std::string HandshakeFailure(rdpSettings const& settings, std::string const& protocols) {
  auto const selected = freerdp_settings_get_uint32(&settings, FreeRDP_SelectedProtocol);
  return std::format("TLS handshake failed: client requested {}, server selected {}", protocols,
                     ProtocolNames(selected, !selected));
}
void ReportDisconnect(Diagnostics const& diagnostics, Activation const& activation, UINT32 code, bool pending,
                      char const* error) {
  auto const activated = activation.Activated();
  if (ExpectedDisconnect(code))
    diagnostics.Log(SDLRDP_LOG_INFO, activated ? std::format("Peer disconnected: {}.", error)
                                               : std::format("Connection closed before activation: {}.", error));
  else if (activation.Active() && pending)
    diagnostics.Log(SDLRDP_LOG_ERROR, std::format("Peer transport failed with pending data: {}.", error));
  else if (!activated)
    diagnostics.Log(SDLRDP_LOG_INFO, code ? std::format("Connection closed before activation: {}.", error)
                                          : "Connection closed before activation.");
}
}
TransportEnd::TransportEnd(PeerLink& link, Activation const& activation, Authenticator& authenticator,
                           PeerFrames const& frames, FrameStore& store, Diagnostics const& diagnostics) noexcept
    : _link { link }, _activation{ activation }, _authenticator{ authenticator }, _frames{ frames }, _store{ store },
      _diagnostics{ diagnostics } { }
bool TransportEnd::SecurityEnded() const {
  if (!NegotiationRefused() && !TlsHandshakeFailed()) return false;
  auto const& settings  = _link.Settings();
  // FreeRDP 3.15 nego.c publishes requestedProtocols even after negotiation fails.
  auto const  requested = freerdp_settings_get_uint32(&settings, FreeRDP_RequestedProtocols);
  auto const  protocols = ProtocolNames(requested, !requested);
  _diagnostics.Log(SDLRDP_LOG_WARN,
                   NegotiationRefused() ? Refusal(settings, protocols) : HandshakeFailure(settings, protocols));
  return true;
}
bool TransportEnd::PendingOutput() const {
  auto const frame = _store.Lock();
  return _frames.Pending(frame) || _link.WriteBlocked();
}
void TransportEnd::Report() {
  if (SecurityEnded()) return;
  _authenticator.End();
  auto const code = freerdp_get_last_error(&_link.Context());
  ReportDisconnect(_diagnostics, _activation, code, PendingOutput(), freerdp_get_last_error_name(code));
}
}
