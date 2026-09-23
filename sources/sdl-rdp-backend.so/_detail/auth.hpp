#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/peer.h>
#include <string>

namespace Backend {
struct AuthenticationState {
  bool              checked = false, rejected = false, hash_attempted = false;
  std::string user, domain;
};
struct Authentication {
public:
  Authentication(Authentication const&)            = delete;
  Authentication& operator=(Authentication const&) = delete;
  Authentication(Authentication&&)                 = delete;
  Authentication& operator=(Authentication&&)      = delete;
  explicit Authentication(sdlrdp_config const& value);
  ~Authentication();
  sdlrdp_config               config;
  std::string user, password, domain;
};
BOOL            Authenticate(freerdp_peer* client, SEC_WINNT_AUTH_IDENTITY const*, BOOL automatic);
bool            AuthenticateSettings(freerdp_peer* client);
void            AuthenticationIdentity(freerdp_peer* client, sdlrdp_event& event);
SECURITY_STATUS AuthenticationHash(void* /*raw*/, SEC_WINNT_AUTH_IDENTITY const*, SecBuffer const* /*unused*/,
                                   BYTE const* /*unused*/, BYTE const* /*unused*/, SecBuffer const* /*unused*/, BYTE* /*response*/);
}
