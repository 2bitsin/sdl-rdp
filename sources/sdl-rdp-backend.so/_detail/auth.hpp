#pragma once
#include "authentication-state.hpp"
#include "pinned.hpp"
#include "sdl-rdp-backend.h"

#include <freerdp/peer.h>

namespace Backend {
class Configuration;
class Diagnostics;
class PeerLink;
class Authenticator : private Pinned {
public:
       Authenticator(PeerLink& link, Configuration const& configuration, Diagnostics const& diagnostics) noexcept;
  BOOL Logon(BOOL automatic);
  bool VerifySettings();
  bool Hash(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response);
  void End();

private:
  void Reject();
  bool Verify(char const* domain, char const* user, char const* password);
  bool Denied();
  bool ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response);
  PeerLink&            _link;
  Configuration const& _configuration;
  Diagnostics const&   _diagnostics;
  AuthenticationState  _state;
};
void AuthenticationIdentity(freerdp_peer const& client, sdlrdp_event& event);
}
