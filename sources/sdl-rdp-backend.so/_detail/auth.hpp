#pragma once
#include "sdl-rdp-backend.h"

#include <freerdp/peer.h>
#include <string>

namespace Backend {
struct AuthenticationState {
  bool        checked        = false;
  bool        rejected       = false;
  bool        hash_attempted = false;
  std::string user;
  std::string domain;
};
struct Authentication {
public:
  Authentication(Authentication const&) = delete;
  Authentication(Authentication&&)      = delete;
  explicit Authentication(sdlrdp_config const& value);
  ~Authentication();
  Authentication& operator = (Authentication const&) = delete;
  Authentication& operator = (Authentication&&)      = delete;
  sdlrdp_config const& Config() const { return config; }

private:
  sdlrdp_config config;
  std::string   user;
  std::string   password;
  std::string   domain;
};
BOOL Authenticate(freerdp_peer* client, SEC_WINNT_AUTH_IDENTITY const*, BOOL automatic);
bool AuthenticateSettings(freerdp_peer* client);
void AuthenticationIdentity(freerdp_peer* client, sdlrdp_event& event);
SECURITY_STATUS AuthenticationHash(void* /*raw*/, SEC_WINNT_AUTH_IDENTITY const*, SecBuffer const* /*unused*/,
                                   BYTE const* /*unused*/, BYTE const* /*unused*/, SecBuffer const* /*unused*/,
                                   BYTE* /*response*/);
}
