#include <sdl-rdp/auth/authenticator.hpp>

#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/auth/identity.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <tuple>

namespace sdl_rdp::auth::detail::authenticator {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::diagnostics::AuthenticationRejectedLogging;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::Get;
using sdl_rdp::freerdp_facade::NtOwfV2;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Utf16;
using sdl_rdp::utilities::Wipe;
using sdl_rdp::utilities::WipedString;

namespace {
struct SettingsPassword : private Pinned {
public:
  explicit SettingsPassword(rdpSettings& value) : settings{ value } { }
           ~SettingsPassword() {
    auto* password = freerdp_settings_get_string_writable(&settings, FreeRDP_Password);
    if (password) Wipe(std::as_writable_bytes(std::span{ password, std::strlen(password) }));
    // FreeRDP 3.32 include/freerdp/settings.h:553: set_string copies input; nullptr removes the old entry.
    auto const cleared = freerdp_settings_set_string(&settings, FreeRDP_Password, nullptr);
    Ensures(cleared, "password cleared");
  }

private:
  rdpSettings& settings;
};
}
Authenticator::Authenticator(PeerLink& link, Configuration const& configuration,
                             Diagnostics const& diagnostics) noexcept
    : _link{ link }, _configuration{ configuration }, _diagnostics{ diagnostics },
      _credentials{ configuration.CertificateDirectory() } { }
auto Authenticator::InstallCredentials(rdpSettings& settings) const -> void {
  InstallServerCredentials(settings, _credentials);
}
auto Authenticator::Auth() const noexcept -> AuthMode {
  return _configuration.Auth();
}
auto Authenticator::Reject() -> void {
  if (_state.TestAndSetRejected()) return;
  _link.Client().authenticated = false;
  AuthenticationRejectedLogging();
  auto const user = QualifiedName(_state.Domain(), _state.User());
  _diagnostics.Log(LogLevel::Warn,
                   std::format("Authentication rejected: user \"{}\" from {}", user, _link.Client().hostname));
}
auto Authenticator::Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool {
  SettingsPassword const clear  { _link.Settings() };
  auto&                  client = _link.Client();
  _state.Identify(user, domain);
  sspi_FreeAuthIdentity(&client.identity);
  if (sspi_SetAuthIdentityA(&client.identity, user.c_str(), domain.c_str(), nullptr) <= 0) {
    Reject();
    return false;
  }
  WipedString const plain    { password };
  bool const        accepted = _configuration.Credentials().Verifies(domain, user, plain.Text());
  if (!accepted) Reject();
  client.authenticated = accepted;
  return accepted;
}
auto Authenticator::Denied() -> bool {
  _link.Client().authenticated = false;
  _link.Refuse(ERRINFO_SERVER_DENIED_CONNECTION);
  return false;
}
auto Authenticator::Logon(bool automatic) -> bool {
  // FreeRDP 3.32 peer.c:846 drops a failed Logon unannounced; the refusal waits for activation, where ERRINFO reaches.
  if (!automatic || _configuration.Config().auth == AuthMode::None) return true;
  // FreeRDP 3.32 nla.c:1494 stores delegated credentials in settings, not nla_get_identity().
  std::ignore = _state.TestAndSetChecked();
  auto const& settings = _link.Settings();
  auto const  verified = [&] {
    return Verify(std::string{ Get(settings, FreeRDP_Domain).value_or("") },
                  std::string{ Get(settings, FreeRDP_Username).value_or("") },
                  Get(settings, FreeRDP_Password).value_or(""));
  };
  if (!Contained(false, verified, Failures("Logon verification"))) Reject();
  return true;
}
auto Authenticator::Unauthenticated(std::string const& domain, std::string const& user) -> bool {
  auto& client = _link.Client();
  client.authenticated = false;
  return sspi_SetAuthIdentityA(&client.identity, user.c_str(), domain.c_str(), nullptr) > 0;
}
auto Authenticator::VerifySettings() -> bool {
  auto&                  client   = _link.Client();
  auto&                  settings = _link.Settings();
  SettingsPassword const clear    { settings };
  if (_state.TestAndSetChecked()) {
    if (!_state.Rejected()) return true;
    std::ignore = Denied();
    Ensures(!client.authenticated, "a rejected peer is not authenticated at activation");
    return false;
  }
  auto const verified = [&] -> std::optional<bool> {
    auto const domain = std::string{ Get(settings, FreeRDP_Domain).value_or("") };
    auto const user   = std::string{ Get(settings, FreeRDP_Username).value_or("") };
    if (_configuration.Config().auth == AuthMode::None) return Unauthenticated(domain, user);
    return Verify(domain, user, Get(settings, FreeRDP_Password).value_or("")) || Denied();
  };
  if (auto const outcome = Contained(std::optional<bool>{ }, verified, Failures("Settings verification")))
    return *outcome;
  Reject();
  return Denied();
}
auto Authenticator::NtHash(std::string const& domain, std::string const& user) const -> std::optional<NtOwf> {
  return _configuration.Credentials().NtHash(domain, user);
}
auto Authenticator::ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response) -> bool {
  auto const names = ClientNames(identity);
  _state.Identify(names.user, names.domain);
  auto const hash = NtHash(_state.Domain(), _state.User());
  if (!hash) return false;
  // FreeRDP 3.32 ntlm_compute.c:513 takes the NTLMv2 response key, not the NT hash.
  auto const key = NtOwfV2(*hash, Utf16(_state.User()), Utf16(_state.Domain()));
  std::ranges::copy(key.Bytes(), response.begin());
  return true;
}
auto Authenticator::Hash(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response) -> bool {
  _state.AttemptHash();
  bool const keyed = Contained(false, [&] { return ResponseKey(identity, response); }, Failures("NTLM response key"));
  if (!keyed) Reject();
  return keyed;
}
auto Authenticator::Failures(OperationName operation) const noexcept -> FailureLog {
  return FailureLog{ _diagnostics, operation, LogLevel::Warn };
}
auto Authenticator::End() -> void {
  if (_state.Abandoned()) Reject();
}
auto AuthenticationIdentity(freerdp_peer const& client) -> ClientIdentity {
  auto names = ClientNames(client.identity);
  return { .user          = std::move(names.user),
           .domain        = std::move(names.domain),
           .authenticated = client.authenticated != 0 };
}
}
