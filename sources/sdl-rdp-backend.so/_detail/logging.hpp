#pragma once
#include "sdl-rdp-backend.h"

#include <freerdp/settings.h>
#include <winpr/wlog.h>
#include <map>
#include <mutex>
#include <thread>
namespace Backend {
// MS-RDPBCGR 2.2.1.1.1 requestedProtocols (FreeRDP keeps these constants private).
inline constexpr unsigned SecurityTls = 0x01, SecurityNla = 0x02, SecurityRdstls = 0x04, SecurityNlaExt = 0x08,
                          SecurityRdsaad = 0x10;
auto PeerNegotiationLogging(rdpSettings const* settings) -> void;
auto NegotiationRefused()                                -> bool;
auto TlsHandshakeFailed()                                -> bool;
auto ExpectedDisconnect(unsigned code)                   -> bool;
auto AuthenticationRejectedLogging()                     -> void;
auto ResetAuthenticationLogging()                        -> void;
class LogRoute {
public:
  struct Filter {
    bool               authentication_failed{ false   };
    rdpSettings const* peer_settings        { nullptr };
    bool               negotiation_failed   { false   };
    bool               handshake_failed     { false   };
  };
  explicit    LogRoute(sdlrdp_config const& config);
              LogRoute(LogRoute const&)               = delete;
              LogRoute(LogRoute&&)                    = delete;
              ~LogRoute();
  auto        operator=(LogRoute const&) -> LogRoute& = delete;
  auto        operator=(LogRoute&&)      -> LogRoute& = delete;
  static auto WithFilter(auto operation) -> decltype(auto) {
    auto&                  routing = Shared();
    std::scoped_lock const lock(routing.guard);
    return operation(routing.filters[std::this_thread::get_id()]);
  }

private:
  struct Routing {
    std::recursive_mutex              guard;
    std::once_flag                    installed;
    LogRoute*                         active   { nullptr };
    std::map<std::thread::id, Filter> filters;
  };
  static auto Shared()                                                -> Routing&;
  static auto Forward(wLogMessage const* message)                     -> BOOL;
  static auto Install()                                               -> void;
  auto        Deliver(sdlrdp_log_level level, char const* text) const -> void;
  decltype(sdlrdp_config::log) callback;
  void*                        user;
};
}
