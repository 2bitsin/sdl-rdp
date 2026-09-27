#include <sdl-rdp/peer/transport-end.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

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
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::diagnostics::NegotiationRefused;
using sdl_rdp::diagnostics::SecurityNla;
using sdl_rdp::diagnostics::SecurityNlaExt;
using sdl_rdp::diagnostics::SecurityRdsaad;
using sdl_rdp::diagnostics::SecurityRdstls;
using sdl_rdp::diagnostics::SecurityTls;
using sdl_rdp::diagnostics::TlsHandshakeFailed;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::Cause;
using sdl_rdp::freerdp_facade::LastError;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::SettingsReader;

namespace {
constexpr std::array<std::pair<std::uint32_t, std::string_view>, 5> ProtocolFlags{ { { SecurityTls   , "TLS"     },
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
auto Refusal(SettingsReader settings, std::string const& protocols) -> std::string {
  auto const offered = (settings.Get(BoolKey::TlsSecurity) ? SecurityTls : 0)
                       | (settings.Get(BoolKey::NlaSecurity) ? SecurityNla : 0);
  return std::format("Connection refused: client requested {}, server offers {}", protocols,
                     ProtocolNames(offered, settings.Get(BoolKey::RdpSecurity)));
}
auto HandshakeFailure(SettingsReader settings, std::string const& protocols) -> std::string {
  auto const selected = settings.Get(NumberKey::SelectedProtocol);
  return std::format("TLS handshake failed: client requested {}, server selected {}", protocols,
                     ProtocolNames(selected, !selected));
}
auto ReportDisconnect(Diagnostics const& diagnostics, Activation const& activation, LastError const& error,
                      bool pending) -> void {
  auto const activated = activation.Activated();
  if (ExpectedDisconnect(error.cause))
    diagnostics.Log(LogLevel::Info, activated ? std::format("Peer disconnected: {}.", error.name)
                                              : std::format("Connection closed before activation: {}.", error.name));
  else if (activation.Active() && pending)
    diagnostics.Log(LogLevel::Error, std::format("Peer transport failed with pending data: {}.", error.name));
  else if (!activated)
    diagnostics.Log(LogLevel::Info, error.cause != Cause::None
                                        ? std::format("Connection closed before activation: {}.", error.name)
                                        : "Connection closed before activation.");
}
}
TransportEnd::TransportEnd(PeerLink const& link, Activation const& activation, Authenticator& authenticator,
                           PeerFrames const& frames, FrameStore& store, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _activation{ activation }, _authenticator{ authenticator }, _frames{ frames }, _store{ store },
      _diagnostics{ diagnostics } { }
auto TransportEnd::SecurityEnded() const -> bool {
  if (!NegotiationRefused() && !TlsHandshakeFailed()) return false;
  auto const settings = _link.Connection().Settings();
  // FreeRDP 3.32 nego.c:1663 publishes requestedProtocols even after a failure response.
  auto const requested = settings.Get(NumberKey::RequestedProtocols);
  auto const protocols = ProtocolNames(requested, !requested);
  _diagnostics.Log(LogLevel::Warn,
                   NegotiationRefused() ? Refusal(settings, protocols) : HandshakeFailure(settings, protocols));
  return true;
}
auto TransportEnd::PendingOutput() const -> bool {
  auto const frame = _store.Lock();
  return _frames.Pending(frame) || _link.Connection().WriteBlocked();
}
auto TransportEnd::Report() -> void {
  if (SecurityEnded()) return;
  _authenticator.End();
  auto const error = _link.Connection().Error();
  ReportDisconnect(_diagnostics, _activation, error, PendingOutput());
}
}
