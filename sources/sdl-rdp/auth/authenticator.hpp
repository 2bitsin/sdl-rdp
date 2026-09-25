#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/auth/credentials.hpp>
#include <sdl-rdp/auth/state.hpp>
#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/peer.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Backend {
class Configuration;
class Diagnostics;
class FailureLog;
class PeerLink;
// NTLM keys are an MD5 digest wide (MS-NLMP 3.3.2).
using NtKey = std::span<std::uint8_t, 16>;
class Authenticator : private Pinned {
public:
       Authenticator(PeerLink& link, Configuration const& configuration, Diagnostics const& diagnostics) noexcept;
  auto Logon(bool automatic)                                         -> bool;
  auto VerifySettings()                                              -> bool;
  auto Hash(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response) -> bool;
  auto End()                                                         -> void;
  auto InstallCredentials(rdpSettings& settings) const               -> void;
  auto Auth() const noexcept                                         -> sdlrdp_auth;

private:
  auto Reject()                                                                              -> void;
  auto Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool;
  auto Unauthenticated(std::string const& domain, std::string const& user)                   -> bool;
  auto Denied()                                                                              -> bool;
  auto ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response)                  -> bool;
  auto NtHash(std::string const& domain, std::string const& user) const
      -> std::optional<sdl_rdp::freerdp_facade::NtOwf>;
  auto Failures(OperationName operation) const noexcept                                      -> FailureLog;
  PeerLink&              _link;
  Configuration const&   _configuration;
  Diagnostics const&     _diagnostics;
  sdl_rdp::auth::Account _account;
  Credentials            _credentials;
  AuthenticationState    _state;
};
auto AuthenticationIdentity(freerdp_peer const& client, sdlrdp_event& event) -> void;
}
