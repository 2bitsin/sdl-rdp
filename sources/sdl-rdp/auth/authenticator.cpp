#include <sdl-rdp/auth/authenticator.hpp>

#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/auth/identity.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <freerdp/settings.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Backend {
namespace {
struct SettingsPassword {
public:
           SettingsPassword(SettingsPassword const&) = delete;
           SettingsPassword(SettingsPassword&&)      = delete;
  explicit SettingsPassword(rdpSettings& value) : settings{ value } { }
           ~SettingsPassword() {
    auto* password = freerdp_settings_get_string_writable(&settings, FreeRDP_Password);
    if (password) OPENSSL_cleanse(password, std::strlen(password));
    // FreeRDP 3.32 include/freerdp/settings.h:553: set_string copies input; nullptr removes the old entry.
    Ensures(freerdp_settings_set_string(&settings, FreeRDP_Password, nullptr), "password cleared");
  }
  auto operator=(SettingsPassword const&) -> SettingsPassword& = delete;
  auto operator=(SettingsPassword&&)      -> SettingsPassword& = delete;

private:
  rdpSettings& settings;
};
struct PlainPassword {
public:
           PlainPassword(PlainPassword const&) = delete;
           PlainPassword(PlainPassword&&)      = delete;
  explicit PlainPassword(std::string_view text) : value{ text } { }
           ~PlainPassword() {
    OPENSSL_cleanse(value.data(), value.size());
  }
  auto operator=(PlainPassword const&) -> PlainPassword& = delete;
  auto operator=(PlainPassword&&)      -> PlainPassword& = delete;
  auto Text() const                    -> std::string const& {
    return value;
  }

private:
  std::string value;
};
auto Setting(rdpSettings const& settings, FreeRDP_Settings_Keys_String key) -> std::string {
  auto const* value = freerdp_settings_get_string(&settings, key);
  return value ? value : "";
}
// In place in the buffer SettingsPassword scrubs; Verify copies it once, for the terminator, into PlainPassword.
auto Password(rdpSettings const& settings) -> std::string_view {
  auto const* value = freerdp_settings_get_string(&settings, FreeRDP_Password);
  return value ? value : "";
}
}
Authenticator::Authenticator(PeerLink& link, Configuration const& configuration,
                             Diagnostics const& diagnostics) noexcept
    : _link{ link }, _configuration{ configuration }, _diagnostics{ diagnostics }, _account{ configuration.Config() },
      _credentials{ configuration.CertificateDirectory() } { }
auto Authenticator::InstallCredentials(rdpSettings& settings) const -> bool {
  try {
    InstallServerCredentials(settings, _credentials);
    return true;
  } catch (std::runtime_error const&) {
    return false;
  }
}
auto Authenticator::Auth() const noexcept -> sdlrdp_auth {
  return _configuration.Auth();
}
auto Authenticator::Reject() -> void {
  if (_state.TestAndSetRejected()) return;
  _link.Client().authenticated = false;
  AuthenticationRejectedLogging();
  auto const user = QualifiedName(_state.Domain(), _state.User());
  _diagnostics.Log(SDLRDP_LOG_WARN,
                   std::format("Authentication rejected: user \"{}\" from {}", user, _link.Client().hostname));
}
auto Authenticator::Verify(std::string const& domain, std::string const& user, std::string_view password) -> bool {
  SettingsPassword const clear  { _link.Settings() };
  auto const&            config = _configuration.Config();
  auto&                  client = _link.Client();
  _state.Identify(user, domain);
  sspi_FreeAuthIdentity(&client.identity);
  if (sspi_SetAuthIdentityA(&client.identity, user.c_str(), domain.c_str(), nullptr) <= 0) {
    Reject();
    return false;
  }
  PlainPassword const plain    { password };
  bool const          accepted = config.verify
                            ? config.verify(config.auth_user, domain.c_str(), user.c_str(), plain.Text().c_str()) != 0
                            : _account.Verifies(domain, user, plain.Text());
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
  if (!automatic || _configuration.Config().auth == SDLRDP_AUTH_NONE) return true;
  // FreeRDP 3.32 nla.c:1494 stores delegated credentials in settings, not nla_get_identity().
  std::ignore = _state.TestAndSetChecked();
  auto const& settings = _link.Settings();
  try {
    std::ignore = Verify(Setting(settings, FreeRDP_Domain), Setting(settings, FreeRDP_Username), Password(settings));
  } catch (...) {
    Reject();
  }
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
  try {
    auto const domain = Setting(settings, FreeRDP_Domain);
    auto const user   = Setting(settings, FreeRDP_Username);
    if (_configuration.Config().auth == SDLRDP_AUTH_NONE) return Unauthenticated(domain, user);
    return Verify(domain, user, Password(settings)) || Denied();
  } catch (...) {
    Reject();
    return Denied();
  }
}
auto Authenticator::NtHash(std::string const& domain, std::string const& user) const
    -> std::optional<sdl_rdp::freerdp_facade::NtOwf> {
  auto const& config = _configuration.Config();
  if (!config.lookup) return _account.NtHash(domain, user);
  sdl_rdp::freerdp_facade::NtOwf hash;
  if (!config.lookup(config.auth_user, domain.c_str(), user.c_str(), hash.Bytes().data())) return std::nullopt;
  return hash;
}
auto Authenticator::ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response) -> bool {
  auto const names = ClientNames(identity);
  _state.Identify(names.user, names.domain);
  auto const hash = NtHash(_state.Domain(), _state.User());
  if (!hash) return false;
  // FreeRDP 3.32 ntlm_compute.c:513 takes the NTLMv2 response key, not the NT hash.
  auto const key = sdl_rdp::freerdp_facade::NtOwfV2(*hash, Utf16(_state.User()), Utf16(_state.Domain()));
  std::ranges::copy(key.Bytes(), response.begin());
  return true;
}
auto Authenticator::Hash(SEC_WINNT_AUTH_IDENTITY const& identity, NtKey response) -> bool {
  _state.AttemptHash();
  try {
    bool const result = ResponseKey(identity, response);
    if (!result) Reject();
    return result;
  } catch (...) {
    Reject();
    return false;
  }
}
auto Authenticator::End() -> void {
  if (_state.Abandoned()) Reject();
}
auto AuthenticationIdentity(freerdp_peer const& client, sdlrdp_event& event) -> void {
  Expects(event.type == SDLRDP_CONNECTED, "identity is attached to a connection event");
  auto const& identity = client.identity;
  auto const  names    = ClientNames(identity);
  CopyTerminated(event.connected.user, names.user);
  CopyTerminated(event.connected.domain, names.domain);
  event.connected.authenticated = client.authenticated != 0;
}
}
