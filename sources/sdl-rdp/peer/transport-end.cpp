#include <sdl-rdp/peer/transport-end.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <freerdp/settings.h>
#include <oxbox/utilities/text.hpp>
#include <array>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdl_rdp::peer::detail::transport_end {
using sdl_rdp::diagnostics::ExpectedDisconnect;
using sdl_rdp::diagnostics::NegotiationRefused;
using sdl_rdp::diagnostics::SecurityNla;
using sdl_rdp::diagnostics::SecurityNlaExt;
using sdl_rdp::diagnostics::SecurityRdsaad;
using sdl_rdp::diagnostics::SecurityRdstls;
using sdl_rdp::diagnostics::SecurityTls;
using sdl_rdp::diagnostics::TlsHandshakeFailed;

namespace {
constexpr std::array<std::pair<std::uint32_t, char const*>, 5> ProtocolFlags{ { { SecurityTls   , "TLS"     },
                                                                                { SecurityNla   , "NLA"     },
                                                                                { SecurityNlaExt, "NLA_EXT" },
                                                                                { SecurityRdstls, "RDSTLS"  },
                                                                                { SecurityRdsaad, "RDSAAD"  } } };
auto ProtocolNames(std::uint32_t mask, bool rdp) -> std::string {
  std::vector<std::string_view> names;
  if (rdp) names.emplace_back("RDP");
  names.append_range(ProtocolFlags | std::views::filter([mask](auto entry) { return mask & entry.first; })
                     | std::views::values);
  return oxbox::utilities::Joined(names, "|");
}
auto Refusal(rdpSettings const& settings, std::string const& protocols) -> std::string {
  auto const offered = (freerdp_settings_get_bool(&settings, FreeRDP_TlsSecurity) ? SecurityTls : 0)
                       | (freerdp_settings_get_bool(&settings, FreeRDP_NlaSecurity) ? SecurityNla : 0);
  return std::format("Connection refused: client requested {}, server offers {}", protocols,
                     ProtocolNames(offered, freerdp_settings_get_bool(&settings, FreeRDP_RdpSecurity)));
}
auto HandshakeFailure(rdpSettings const& settings, std::string const& protocols) -> std::string {
  auto const selected = freerdp_settings_get_uint32(&settings, FreeRDP_SelectedProtocol);
  return std::format("TLS handshake failed: client requested {}, server selected {}", protocols,
                     ProtocolNames(selected, !selected));
}
auto ReportDisconnect(Diagnostics const& diagnostics, Activation const& activation, std::uint32_t code, bool pending,
                      std::string_view error) -> void {
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
    : _link{ link }, _activation{ activation }, _authenticator{ authenticator }, _frames{ frames }, _store{ store },
      _diagnostics{ diagnostics } { }
auto TransportEnd::SecurityEnded() const -> bool {
  if (!NegotiationRefused() && !TlsHandshakeFailed()) return false;
  auto const& settings = _link.Settings();
  // FreeRDP 3.32 nego.c:1663 publishes requestedProtocols even after a failure response.
  auto const requested = freerdp_settings_get_uint32(&settings, FreeRDP_RequestedProtocols);
  auto const protocols = ProtocolNames(requested, !requested);
  _diagnostics.Log(SDLRDP_LOG_WARN,
                   NegotiationRefused() ? Refusal(settings, protocols) : HandshakeFailure(settings, protocols));
  return true;
}
auto TransportEnd::PendingOutput() const -> bool {
  auto const frame = _store.Lock();
  return _frames.Pending(frame) || _link.WriteBlocked();
}
auto TransportEnd::Report() -> void {
  if (SecurityEnded()) return;
  _authenticator.End();
  auto const code = freerdp_get_last_error(&_link.Context());
  ReportDisconnect(_diagnostics, _activation, code, PendingOutput(), freerdp_get_last_error_name(code));
}
}
