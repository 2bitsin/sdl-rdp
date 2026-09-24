#pragma once
#include <sdl-rdp/core/authentication-state.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/peer.h>

namespace Backend {
class Configuration;
class Diagnostics;
class PeerLink;
class Authenticator : private Pinned {
public:
       Authenticator(PeerLink& link, Configuration const& configuration, Diagnostics const& diagnostics) noexcept;
  auto Logon(BOOL automatic)                                         -> BOOL;
  auto VerifySettings()                                              -> bool;
  auto Hash(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response) -> bool;
  auto End()                                                         -> void;

private:
  auto Reject()                                                             -> void;
  auto Verify(char const* domain, char const* user, char const* password)   -> bool;
  auto Denied()                                                             -> bool;
  auto ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response) -> bool;
  PeerLink&            _link;
  Configuration const& _configuration;
  Diagnostics const&   _diagnostics;
  AuthenticationState  _state;
};
auto AuthenticationIdentity(freerdp_peer const& client, sdlrdp_event& event) -> void;
}
