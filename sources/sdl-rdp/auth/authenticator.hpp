#pragma once
#include <sdl-rdp/auth/credentials.hpp>
#include <sdl-rdp/auth/state.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/connection-events.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace sdl_rdp::auth::detail::authenticator {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::freerdp_facade::Identity;
using sdl_rdp::freerdp_facade::SettingsView;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Pinned;

class Authenticator : private Pinned {
public:
       Authenticator(PeerLink& link, Configuration const& configuration, Diagnostics const& diagnostics) noexcept;
  auto Logon(bool automatic)                           -> bool;
  auto VerifySettings()                                -> bool;
  auto NtlmHash(Identity const& identity)              -> std::optional<NtOwf>;
  auto NtlmRefused(std::string_view cause)             -> void;
  auto End()                                           -> void;
  auto InstallCredentials(SettingsView settings) const -> void;
  auto Auth() const noexcept                           -> AuthMode;

private:
  auto Reject()                                                                              -> void;
  auto Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool;
  auto Unauthenticated(std::string const& domain, std::string const& user)                   -> bool;
  auto Denied()                                                                              -> bool;
  auto NtHash(std::string const& domain, std::string const& user) const                      -> std::optional<NtOwf>;
  auto Failures(OperationName operation) const noexcept                                      -> FailureLog;
  PeerLink&            _link;
  Configuration const& _configuration;
  Diagnostics const&   _diagnostics;
  Credentials          _credentials;
  AuthenticationState  _state;
};
auto QualifiedName(std::string_view domain, std::string_view user) -> std::string;
}

namespace sdl_rdp::auth {
using detail::authenticator::Authenticator;
using detail::authenticator::QualifiedName;
}
