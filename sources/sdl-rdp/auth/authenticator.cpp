#include <sdl-rdp/auth/authenticator.hpp>

#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/auth/identity.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <freerdp/settings.h>
#include <openssl/crypto.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Backend {
namespace {
struct NtHash {
public:
  NtHash(NtHash const&) = delete;
  NtHash(NtHash&&)      = delete;
  NtHash()              = default;
  ~NtHash() {
    OPENSSL_cleanse(bytes.data(), bytes.size());
  }
  auto operator=(NtHash const&) -> NtHash& = delete;
  auto operator=(NtHash&&)      -> NtHash& = delete;
  auto Data()                   -> std::uint8_t* {
    return bytes.data();
  }

private:
  std::array<std::uint8_t, 16> bytes{ };
};
struct SettingsPassword {
public:
           SettingsPassword(SettingsPassword const&) = delete;
           SettingsPassword(SettingsPassword&&)      = delete;
  explicit SettingsPassword(rdpSettings* value) : settings{ value } { }
           ~SettingsPassword() {
    auto* password = freerdp_settings_get_string_writable(settings, FreeRDP_Password);
    if (password) OPENSSL_cleanse(password, std::strlen(password));
    // FreeRDP 3.32 include/freerdp/settings.h:553: set_string copies input; nullptr removes the old entry.
    Ensures(freerdp_settings_set_string(settings, FreeRDP_Password, nullptr), "password cleared");
  }
  auto operator=(SettingsPassword const&) -> SettingsPassword& = delete;
  auto operator=(SettingsPassword&&)      -> SettingsPassword& = delete;

private:
  rdpSettings* settings;
};
struct PlainPassword {
public:
           PlainPassword(PlainPassword const&) = delete;
           PlainPassword(PlainPassword&&)      = delete;
  explicit PlainPassword(char const* text) : value{ text } { }
           ~PlainPassword() {
    OPENSSL_cleanse(value.data(), value.size());
  }
  auto operator=(PlainPassword const&) -> PlainPassword& = delete;
  auto operator=(PlainPassword&&)      -> PlainPassword& = delete;
  auto Text() const                    -> char const* {
    return value.c_str();
  }

private:
  std::string value;
};
auto Setting(freerdp_peer const& client, FreeRDP_Settings_Keys_String key) -> char const* {
  auto const* value = freerdp_settings_get_string(client.context->settings, key);
  return value ? value : "";
}
auto Utf16(std::string const& text) -> std::vector<std::uint8_t> {
  return TranscodeRange<std::vector<std::uint8_t>>(std::as_bytes(std::span(text)), { }, Utf16Little);
}
auto NtlmResponseKey(AuthenticationState const& identity, std::uint8_t* nt_hash_v1, std::uint8_t* response) -> bool {
  // FreeRDP 3.32 ntlm_compute.c:513 takes the NTLMv2 response key, not the NT hash, and needs SEC_E_OK.
  auto user          = Utf16(identity.User());
  auto domain        = Utf16(identity.Domain());
  auto user_length   = user.size();
  auto domain_length = domain.size();
  user.resize(user_length + sizeof(char16_t));
  domain.resize(domain_length + sizeof(char16_t));
  using oxbox::utilities::SpanCast;
  return NTOWFv2FromHashW(nt_hash_v1, SpanCast<std::uint16_t>(std::span(user)).data(), user_length,
                          SpanCast<std::uint16_t>(std::span(domain)).data(), domain_length, response);
}
}
Authenticator::Authenticator(PeerLink& link, Configuration const& configuration,
                             Diagnostics const& diagnostics) noexcept
    : _link{ link }, _configuration{ configuration }, _diagnostics{ diagnostics },
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
auto Authenticator::Verify(char const* domain, char const* user, char const* password) -> bool {
  SettingsPassword const clear  { &_link.Settings() };
  auto const&            config = _configuration.Config();
  auto&                  client = _link.Client();
  _state.Identify(user, domain);
  sspi_FreeAuthIdentity(&client.identity);
  if (sspi_SetAuthIdentityA(&client.identity, user, domain, nullptr) <= 0) {
    Reject();
    return false;
  }
  PlainPassword const plain    { password };
  bool const          accepted = config.verify ? config.verify(config.auth_user, domain, user, plain.Text()) != 0
                                               : sdlrdp_verify_pair(&config, domain, user, plain.Text()) != 0;
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
  auto& client = _link.Client();
  try {
    std::ignore = Verify(Setting(client, FreeRDP_Domain), Setting(client, FreeRDP_Username),
                         Setting(client, FreeRDP_Password));
  } catch (...) {
    Reject();
  }
  return true;
}
auto Authenticator::VerifySettings() -> bool {
  auto&                  client = _link.Client();
  SettingsPassword const clear  { client.context->settings };
  if (_state.TestAndSetChecked()) {
    if (!_state.Rejected()) return true;
    std::ignore = Denied();
    Ensures(!client.authenticated, "a rejected peer is not authenticated at activation");
    return false;
  }
  try {
    auto const* domain = Setting(client, FreeRDP_Domain);
    auto const* user   = Setting(client, FreeRDP_Username);
    if (_configuration.Config().auth == SDLRDP_AUTH_NONE) {
      client.authenticated = false;
      return sspi_SetAuthIdentityA(&client.identity, user, domain, nullptr) > 0;
    }
    return Verify(domain, user, Setting(client, FreeRDP_Password)) || Denied();
  } catch (...) {
    Reject();
    return Denied();
  }
}
auto Authenticator::ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, std::uint8_t* response) -> bool {
  auto const names = ClientNames(identity);
  _state.Identify(names.user, names.domain);
  auto const& config = _configuration.Config();
  auto const* domain = _state.Domain().c_str();
  auto const* user   = _state.User().c_str();
  NtHash      hash;
  bool const  known  = config.lookup ? config.lookup(config.auth_user, domain, user, hash.Data()) != 0
                                     : sdlrdp_lookup_pair(&config, domain, user, hash.Data()) != 0;
  return known && NtlmResponseKey(_state, hash.Data(), response);
}
auto Authenticator::Hash(SEC_WINNT_AUTH_IDENTITY const& identity, std::uint8_t* response) -> bool {
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
