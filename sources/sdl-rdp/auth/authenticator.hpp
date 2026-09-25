#pragma once
#include <sdl-rdp/auth/credentials.hpp>
#include <sdl-rdp/auth/state.hpp>
#include <sdl-rdp/configuration/auth-mode.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/peer.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sdl_rdp::auth::detail::authenticator {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Pinned;

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
  auto Auth() const noexcept                                         -> AuthMode;

private:
  auto Reject()                                                                              -> void;
  auto Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool;
  auto Unauthenticated(std::string const& domain, std::string const& user)                   -> bool;
  auto Denied()                                                                              -> bool;
  auto ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response)                  -> bool;
  auto NtHash(std::string const& domain, std::string const& user) const                      -> std::optional<NtOwf>;
  auto Failures(OperationName operation) const noexcept                                      -> FailureLog;
  PeerLink&            _link;
  Configuration const& _configuration;
  Diagnostics const&   _diagnostics;
  Credentials          _credentials;
  AuthenticationState  _state;
};
// Who the client said it was, and whether the server accepted it.
struct ClientIdentity {
  std::string user;
  std::string domain;
  bool        authenticated{ };
};
auto AuthenticationIdentity(freerdp_peer const& client) -> ClientIdentity;
}

namespace sdl_rdp::auth {
using detail::authenticator::AuthenticationIdentity;
using detail::authenticator::Authenticator;
using detail::authenticator::ClientIdentity;
using detail::authenticator::NtKey;
}
