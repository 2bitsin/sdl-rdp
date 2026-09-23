#pragma once
#include "sdl-rdp-backend.h"

#include <freerdp/settings.h>
#include <map>
#include <mutex>
#include <thread>
#include <winpr/wlog.h>
namespace Backend {
// MS-RDPBCGR 2.2.1.1.1 requestedProtocols (FreeRDP keeps these constants private).
inline constexpr unsigned SecurityTls = 0x01, SecurityNla = 0x02, SecurityRdstls = 0x04, SecurityNlaExt = 0x08,
                          SecurityRdsaad = 0x10;
void PeerNegotiationLogging(rdpSettings const* settings);
bool NegotiationRefused();
bool TlsHandshakeFailed();
bool ExpectedDisconnect(unsigned code);
void AuthenticationRejectedLogging();
void ResetAuthenticationLogging();
class LogRoute {
public:
  struct Filter {
    bool               authentication_failed{ false };
    rdpSettings const* peer_settings        { nullptr };
    bool               negotiation_failed   { false };
    bool               handshake_failed     { false };
  };
  explicit LogRoute(sdlrdp_config const& config);
  LogRoute(LogRoute const&) = delete;
  LogRoute(LogRoute&&)      = delete;
  ~LogRoute();
  LogRoute& operator = (LogRoute const&) = delete;
  LogRoute& operator = (LogRoute&&)      = delete;
  static auto WithFilter(auto operation) {
    auto& routing = Shared();
    std::scoped_lock const lock(routing.guard);
    return operation(routing.filters[std::this_thread::get_id()]);
  }

private:
  struct Routing {
    std::recursive_mutex guard;
    std::once_flag       installed;
    LogRoute* active{ nullptr };
    std::map<std::thread::id, Filter> filters;
  };
  static Routing& Shared();
  static BOOL Forward(wLogMessage const* message);
  static void Install();
  void (*callback)(void*, sdlrdp_log_level, char const*);
  void* user;
};
}
