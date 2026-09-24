#include "_detail/auth.hpp"

#include "_detail/auth-identity.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/logging.hpp"
#include "_detail/peer-link.hpp"

#include <freerdp/settings.h>
#include <openssl/crypto.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <cstring>

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
  auto Data()                   -> BYTE* {
    return bytes.data();
  }

private:
  std::array<BYTE, 16> bytes{ };
};
struct SettingsPassword {
public:
           SettingsPassword(SettingsPassword const&) = delete;
           SettingsPassword(SettingsPassword&&)      = delete;
  explicit SettingsPassword(rdpSettings* value) : settings{ value } { }
           ~SettingsPassword() {
    auto* password = freerdp_settings_get_string_writable(settings, FreeRDP_Password);
    if (password) OPENSSL_cleanse(password, std::strlen(password));
    // FreeRDP 3.15 include/freerdp/settings.h: set_string copies input; NULL removes the old entry.
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
auto Utf16(std::string const& text) -> std::vector<BYTE> {
  return TranscodeRange<std::vector<BYTE>>(std::as_bytes(std::span(text)), { }, Utf16Little);
}
auto NtlmResponseKey(AuthenticationState const& identity, BYTE* nt_hash_v1, BYTE* response) -> bool {
  // FreeRDP 3.15's NTLM callback consumes a response key and treats nonzero as success.
  auto user          = Utf16(identity.User());
  auto domain        = Utf16(identity.Domain());
  auto user_length   = user.size();
  auto domain_length = domain.size();
  user.resize(user_length + sizeof(WCHAR));
  domain.resize(domain_length + sizeof(WCHAR));
  using oxbox::utilities::SpanCast;
  return NTOWFv2FromHashW(nt_hash_v1, SpanCast<uint16_t>(std::span(user)).data(), user_length,
                          SpanCast<uint16_t>(std::span(domain)).data(), domain_length, response);
}
}
Authenticator::Authenticator(PeerLink& link, Configuration const& configuration,
                             Diagnostics const& diagnostics) noexcept
    : _link{ link }, _configuration{ configuration }, _diagnostics{ diagnostics } { }
auto Authenticator::Reject() -> void {
  if (_state.TestAndSetRejected()) return;
  _link.Client().authenticated = FALSE;
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
  _link.Refuse(ERRINFO_SERVER_DENIED_CONNECTION);
  return false;
}
auto Authenticator::Logon(BOOL automatic) -> BOOL {
  if (!automatic || _configuration.Config().auth == SDLRDP_AUTH_NONE) return FALSE;
  // FreeRDP 3.15 stores delegated credentials in settings, not nla_get_identity().
  std::ignore = _state.TestAndSetChecked();
  auto& client = _link.Client();
  try {
    return Verify(Setting(client, FreeRDP_Domain), Setting(client, FreeRDP_Username),
                  Setting(client, FreeRDP_Password));
  } catch (...) {
    Reject();
    return FALSE;
  }
}
auto Authenticator::VerifySettings() -> bool {
  auto&                  client = _link.Client();
  SettingsPassword const clear  { client.context->settings };
  if (_state.TestAndSetChecked()) return _state.Rejected() ? Denied() : true;
  try {
    auto const* domain = Setting(client, FreeRDP_Domain);
    auto const* user   = Setting(client, FreeRDP_Username);
    if (_configuration.Config().auth == SDLRDP_AUTH_NONE)
      return sspi_SetAuthIdentityA(&client.identity, user, domain, nullptr) > 0;
    return Verify(domain, user, Setting(client, FreeRDP_Password)) || Denied();
  } catch (...) {
    Reject();
    return Denied();
  }
}
auto Authenticator::ResponseKey(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response) -> bool {
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
auto Authenticator::Hash(SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response) -> bool {
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
  std::strncpy(event.connected.user, names.user.c_str(), sizeof(event.connected.user) - 1);
  std::strncpy(event.connected.domain, names.domain.c_str(), sizeof(event.connected.domain) - 1);
  event.connected.authenticated = client.authenticated != FALSE;
}
}
