#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/freerdp-facade/log-registration.hpp>

#include <freerdp/settings.h>
#include <winpr/wlog.h>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace Backend {
// MS-RDPBCGR 2.2.1.1.1 requestedProtocols (FreeRDP keeps these constants private).
inline constexpr std::uint32_t SecurityTls = 0x01, SecurityNla = 0x02, SecurityRdstls = 0x04, SecurityNlaExt = 0x08,
                               SecurityRdsaad = 0x10;
auto PeerNegotiationLogging(rdpSettings const& settings) -> void;
auto NegotiationRefused()                                -> bool;
auto TlsHandshakeFailed()                                -> bool;
auto ExpectedDisconnect(std::uint32_t code)              -> bool;
auto AuthenticationRejectedLogging()                     -> void;
auto ResetAuthenticationLogging()                        -> void;
class LogRoute {
public:
  struct Filter {
    bool                                                     authentication_failed{ false };
    std::optional<std::reference_wrapper<rdpSettings const>> peer_settings;
    bool                                                     negotiation_failed   { false };
    bool                                                     handshake_failed     { false };
  };
  explicit    LogRoute(sdlrdp_config const& config);
              LogRoute(LogRoute const&)                                               = delete;
              LogRoute(LogRoute&&)                                                    = delete;
              ~LogRoute();
  auto        operator=(LogRoute const&)                                 -> LogRoute& = delete;
  auto        operator=(LogRoute&&)                                      -> LogRoute& = delete;
  auto        Log(sdlrdp_log_level level, std::string const& text) const -> void;
  static auto WithFilter(auto operation)                                 -> decltype(auto) {
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
  static auto Shared()                                                          -> Routing&;
  static auto Forward(wLogMessage const& message)                               -> void;
  static auto Install()                                                         -> void;
  auto        Deliver(sdlrdp_log_level level, wLogMessage const& message) const -> void;
  sdl_rdp::freerdp_facade::LogRegistration _sink;
};
}
