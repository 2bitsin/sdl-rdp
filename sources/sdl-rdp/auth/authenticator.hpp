#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/auth/credentials.hpp>
#include <sdl-rdp/auth/state.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/peer.h>
#include <cstdint>

namespace Backend {
class Configuration;
class Diagnostics;
class PeerLink;
class Authenticator : private Pinned {
public:
       Authenticator(PeerLink& link, Configuration const& configuration, Diagnostics const& diagnostics) noexcept;
  auto Logon(bool automatic)                                                 -> bool;
  auto VerifySettings()                                                      -> bool;
  auto Hash(SEC_WINNT_AUTH_IDENTITY const& identity, std::uint8_t* response) -> bool;
  auto End()                                                                 -> void;
  auto InstallCredentials(rdpSettings& settings) const                       -> bool;
  auto Auth() const noexcept                                                 -> sdlrdp_auth;

private:
  auto Reject()                                                                     -> void;
  auto Verify(char const* domain, char const* user, char const* password)           -> bool;
  auto Denied()                                                                     -> bool;
  auto ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, std::uint8_t* response) -> bool;
  PeerLink&            _link;
  Configuration const& _configuration;
  Diagnostics const&   _diagnostics;
  Credentials          _credentials;
  AuthenticationState  _state;
};
auto AuthenticationIdentity(freerdp_peer const& client, sdlrdp_event& event) -> void;
}
