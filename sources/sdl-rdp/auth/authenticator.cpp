#include <sdl-rdp/auth/authenticator.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>

namespace sdl_rdp::auth::detail::authenticator {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::diagnostics::AuthenticationRejectedLogging;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::Refusal;
using sdl_rdp::freerdp_facade::StringKey;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::WipedString;

namespace {
constexpr OperationName ResponseKeyLookup{ "NTLM response key" };
struct SettingsPassword : private Pinned {
public:
  explicit SettingsPassword(SettingsView value) : settings{ value } { }
           ~SettingsPassword() {
    settings.Wipe(StringKey::Password);
  }

private:
  SettingsView settings;
};
}
Authenticator::Authenticator(PeerLink& link, Configuration const& configuration,
                             Diagnostics const& diagnostics) noexcept
    : _link{ link }, _configuration{ configuration }, _diagnostics{ diagnostics },
      _credentials{ configuration.CertificateDirectory() } { }
auto Authenticator::InstallCredentials(SettingsView settings) const -> void {
  settings.InstallServerCredentials(_credentials.Key(), _credentials.Certificate());
}
auto Authenticator::Auth() const noexcept -> AuthMode {
  return _configuration.Auth();
}
auto Authenticator::Reject() -> void {
  if (_state.TestAndSetRejected()) return;
  auto& connection = _link.Connection();
  connection.SetAuthenticated(false);
  AuthenticationRejectedLogging();
  auto const user = QualifiedName(_state.Domain(), _state.User());
  _diagnostics.Log(LogLevel::Warn,
                   std::format("Authentication rejected: user \"{}\" from {}", user, connection.Hostname()));
}
auto Authenticator::Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool {
  auto&                  connection = _link.Connection();
  SettingsPassword const clear      { connection.Settings() };
  _state.Identify(user, domain);
  connection.Identify({ .user = user, .domain = domain });
  WipedString const plain    { password };
  bool const        accepted = _configuration.Credentials().Verifies(domain, user, plain.Text());
  if (!accepted) Reject();
  connection.SetAuthenticated(accepted);
  return accepted;
}
auto Authenticator::Denied() -> bool {
  auto& connection = _link.Connection();
  connection.SetAuthenticated(false);
  connection.Refuse(Refusal::ServerDenied);
  return false;
}
auto Authenticator::Logon(bool automatic) -> bool {
  // FreeRDP 3.32 peer.c:846 drops a failed Logon unannounced; the refusal waits for activation, where ERRINFO reaches.
  if (!automatic || _configuration.Config().auth == AuthMode::None) return true;
  // FreeRDP 3.32 nla.c:1494 stores delegated credentials in settings, not nla_get_identity().
  std::ignore = _state.TestAndSetChecked();
  auto const settings = _link.Connection().Settings();
  auto const verified = [&] {
    return Verify(std::string{ settings.Get(StringKey::Domain).value_or("") },
                  std::string{ settings.Get(StringKey::Username).value_or("") },
                  settings.Get(StringKey::Password).value_or(""));
  };
  if (!Contained(false, verified, Failures("Logon verification"))) Reject();
  return true;
}
auto Authenticator::Unauthenticated(std::string const& domain, std::string const& user) -> bool {
  auto& connection = _link.Connection();
  connection.SetAuthenticated(false);
  connection.Identify({ .user = user, .domain = domain });
  return true;
}
auto Authenticator::VerifySettings() -> bool {
  auto const             settings = _link.Connection().Settings();
  SettingsPassword const clear    { settings };
  if (_state.TestAndSetChecked()) {
    if (!_state.Rejected()) return true;
    std::ignore = Denied();
    Ensures(!_link.Connection().Authenticated(), "a rejected peer is not authenticated at activation");
    return false;
  }
  auto const verified = [&] -> std::optional<bool> {
    auto const domain = std::string{ settings.Get(StringKey::Domain).value_or("") };
    auto const user   = std::string{ settings.Get(StringKey::Username).value_or("") };
    if (_configuration.Config().auth == AuthMode::None) return Unauthenticated(domain, user);
    return Verify(domain, user, settings.Get(StringKey::Password).value_or("")) || Denied();
  };
  if (auto const outcome = Contained(std::optional<bool>{ }, verified, Failures("Settings verification")))
    return *outcome;
  Reject();
  return Denied();
}
auto Authenticator::NtHash(std::string const& domain, std::string const& user) const -> std::optional<NtOwf> {
  return _configuration.Credentials().NtHash(domain, user);
}
auto Authenticator::NtlmHash(Identity const& identity) -> std::optional<NtOwf> {
  _state.AttemptHash();
  auto const lookup = [&] {
    _state.Identify(identity.user, identity.domain);
    return NtHash(_state.Domain(), _state.User());
  };
  auto       hash   = Contained(std::optional<NtOwf>{ }, lookup, Failures(ResponseKeyLookup));
  if (!hash) Reject();
  return hash;
}
auto Authenticator::NtlmRefused(std::string_view cause) -> void {
  _state.AttemptHash();
  Failures(ResponseKeyLookup)(cause);
  Reject();
}
auto Authenticator::Failures(OperationName operation) const noexcept -> FailureLog {
  return FailureLog{ _diagnostics, operation, LogLevel::Warn };
}
auto Authenticator::End() -> void {
  if (_state.Abandoned()) Reject();
}
auto QualifiedName(std::string_view domain, std::string_view user) -> std::string {
  return domain.empty() ? std::string(user) : std::string(domain) + "\\" + std::string(user);
}
}
