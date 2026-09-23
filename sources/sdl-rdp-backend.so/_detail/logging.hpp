#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/settings.h>
namespace Backend {
// MS-RDPBCGR 2.2.1.1.1 requestedProtocols (FreeRDP keeps these constants private).
inline constexpr unsigned SecurityTls = 0x01, SecurityNla = 0x02, SecurityRdstls = 0x04,
  SecurityNlaExt = 0x08, SecurityRdsaad = 0x10;
void PeerNegotiationLogging(rdpSettings const* settings);
bool NegotiationRefused();
bool TlsHandshakeFailed();
bool ExpectedDisconnect(unsigned code);
void AuthenticationRejectedLogging();
void ResetAuthenticationLogging();
struct LogRoute {
  explicit LogRoute(sdlrdp_config const& config);
  ~LogRoute();
  void (*callback)(void*, sdlrdp_log_level, const char*);
  void* user;
};
}
