#pragma once
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/log-appender.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
namespace sdl_rdp::diagnostics::detail::logging {
using sdl_rdp::freerdp_facade::Cause;
using sdl_rdp::freerdp_facade::LogMessage;
using sdl_rdp::freerdp_facade::SettingsReader;
using sdl_rdp::utilities::Pinned;

// MS-RDPBCGR 2.2.1.1.1 requestedProtocols (FreeRDP keeps these constants private).
inline constexpr std::uint32_t SecurityTls = 0x01, SecurityNla = 0x02, SecurityRdstls = 0x04, SecurityNlaExt = 0x08,
                               SecurityRdsaad = 0x10;
auto PeerNegotiationLogging(SettingsReader settings) -> void;
auto NegotiationRefused()                            -> bool;
auto TlsHandshakeFailed()                            -> bool;
auto ExpectedDisconnect(Cause cause)                 -> bool;
auto AuthenticationRejectedLogging()                 -> void;
auto ResetAuthenticationLogging()                    -> void;
class LogRoute : private Pinned {
public:
  struct Filter {
    bool                          authentication_failed{ false };
    std::optional<SettingsReader> peer_settings;
    bool                          negotiation_failed   { false };
    bool                          handshake_failed     { false };
  };
  explicit    LogRoute(LogSink& sink);
              ~LogRoute();
  auto        Log(LogLevel level, std::string_view text) const -> void;
  static auto WithFilter(auto operation)                       -> decltype(auto) {
    auto&                  routing = Shared();
    std::scoped_lock const lock(routing.guard);
    return operation(routing.filters[std::this_thread::get_id()]);
  }

private:
  struct Routing {
    std::recursive_mutex                                  guard;
    std::once_flag                                        installed;
    std::optional<std::reference_wrapper<LogRoute const>> active;
    std::map<std::thread::id, Filter>                     filters;
  };
  static auto Shared()                           -> Routing&;
  static auto Forward(LogMessage const& message) -> void;
  std::reference_wrapper<LogSink> _sink;
};
}

namespace sdl_rdp::diagnostics {
using detail::logging::AuthenticationRejectedLogging;
using detail::logging::ExpectedDisconnect;
using detail::logging::LogRoute;
using detail::logging::NegotiationRefused;
using detail::logging::PeerNegotiationLogging;
using detail::logging::ResetAuthenticationLogging;
using detail::logging::SecurityNla;
using detail::logging::SecurityNlaExt;
using detail::logging::SecurityRdsaad;
using detail::logging::SecurityRdstls;
using detail::logging::SecurityTls;
using detail::logging::TlsHandshakeFailed;
}
