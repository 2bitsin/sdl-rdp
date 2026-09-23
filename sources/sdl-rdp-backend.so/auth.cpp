#include "_detail/auth.hpp"

#include "_detail/auth-identity.hpp"
#include "_detail/state.hpp"

#include <cstring>
#include <freerdp/settings.h>
#include <openssl/crypto.h>
#include <winpr/ntlm.h>

namespace Backend {
namespace {
struct NtHash {
public:
  NtHash(NtHash const&) = delete;
  NtHash(NtHash&&)      = delete;
  NtHash()              = default;
  ~NtHash() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
  NtHash& operator = (NtHash const&) = delete;
  NtHash& operator = (NtHash&&)      = delete;
  BYTE* Data() { return bytes.data(); }

private:
  std::array<BYTE, 16> bytes{};
};
struct SettingsPassword {
public:
  SettingsPassword(SettingsPassword const&) = delete;
  SettingsPassword(SettingsPassword&&)      = delete;
  explicit SettingsPassword(rdpSettings* value) : settings{ value } {}
  ~SettingsPassword() {
    auto* password = freerdp_settings_get_string_writable(settings, FreeRDP_Password);
    if (password) OPENSSL_cleanse(password, std::strlen(password));
    // FreeRDP 3.15 include/freerdp/settings.h: set_string copies input; NULL removes the old entry.
    Ensures(freerdp_settings_set_string(settings, FreeRDP_Password, nullptr), "password cleared");
  }
  SettingsPassword& operator = (SettingsPassword const&) = delete;
  SettingsPassword& operator = (SettingsPassword&&)      = delete;

private:
  rdpSettings* settings;
};
struct PlainPassword {
public:
  PlainPassword(PlainPassword const&) = delete;
  PlainPassword(PlainPassword&&)      = delete;
  explicit PlainPassword(char const* text) : value{ text } {}
  ~PlainPassword() { OPENSSL_cleanse(value.data(), value.size()); }
  PlainPassword& operator = (PlainPassword const&) = delete;
  PlainPassword& operator = (PlainPassword&&)      = delete;
  char const* Text() const { return value.c_str(); }

private:
  std::string value;
};
void Reject(Peer& peer) {
  if (peer.authentication.rejected) return;
  peer.authentication.rejected = true;
  peer.client->authenticated   = FALSE;
  AuthenticationRejectedLogging();
  peer.owner.Log(SDLRDP_LOG_WARN, std::format("Authentication rejected: user \"{}\" from {}",
                                              QualifiedName(peer.authentication.domain, peer.authentication.user),
                                              peer.client->hostname));
}
bool Verify(Peer& peer, char const* domain, char const* user, char const* password) {
  SettingsPassword const clear{ peer.client->context->settings };
  auto const& config = peer.owner.authentication.Config();
  peer.authentication.user   = user;
  peer.authentication.domain = domain;
  sspi_FreeAuthIdentity(&peer.client->identity);
  if (sspi_SetAuthIdentityA(&peer.client->identity, user, domain, nullptr) <= 0) {
    Reject(peer);
    return false;
  }
  PlainPassword const plain{ password };
  bool const accepted = config.verify ? config.verify(config.auth_user, domain, user, plain.Text()) != 0
                                      : sdlrdp_verify_pair(&config, domain, user, plain.Text()) != 0;
  if (!accepted) Reject(peer);
  peer.client->authenticated = accepted;
  return accepted;
}
bool Denied(freerdp_peer* client) {
  Expects(client != nullptr, "client transport exists");
  Expects(client->context, "client context exists");
  freerdp_set_error_info(client->context->rdp, ERRINFO_SERVER_DENIED_CONNECTION);
  freerdp_send_error_info(client->context->rdp);
  return false;
}
char const* Setting(freerdp_peer* client, FreeRDP_Settings_Keys_String key) {
  auto const* value = freerdp_settings_get_string(client->context->settings, key);
  return value ? value : "";
}
}
BOOL Authenticate(freerdp_peer* client, SEC_WINNT_AUTH_IDENTITY const*, BOOL automatic) {
  auto& peer = Peer::Held(client);
  if (!automatic || peer.owner.authentication.Config().auth == SDLRDP_AUTH_NONE) return FALSE;
  // FreeRDP 3.15 stores delegated credentials in settings, not nla_get_identity().
  peer.authentication.checked = true;
  try {
    auto accepted = Verify(peer, Setting(client, FreeRDP_Domain), Setting(client, FreeRDP_Username),
                           Setting(client, FreeRDP_Password));
    return accepted;
  } catch (...) {
    Reject(peer);
    return FALSE;
  }
}
bool AuthenticateSettings(freerdp_peer* client) {
  auto& peer = Peer::Held(client);
  SettingsPassword const clear{ client->context->settings };
  if (peer.authentication.checked) return peer.authentication.rejected ? Denied(client) : true;
  peer.authentication.checked = true;
  try {
    auto const* domain = Setting(client, FreeRDP_Domain);
    auto const* user   = Setting(client, FreeRDP_Username);
    if (peer.owner.authentication.Config().auth == SDLRDP_AUTH_NONE)
      return sspi_SetAuthIdentityA(&client->identity, user, domain, nullptr) > 0;
    if (Verify(peer, domain, user, Setting(client, FreeRDP_Password))) return true;
    return Denied(client);
  } catch (...) {
    Reject(peer);
    return Denied(client);
  }
}
void AuthenticationIdentity(freerdp_peer* client, sdlrdp_event& event) {
  Expects(client != nullptr, "client transport exists");
  Expects(event.type == SDLRDP_CONNECTED, "identity is attached to a connection event");
  auto& identity = client->identity;
  auto user      = IdentityText(identity.User, identity.UserLength, identity.Flags);
  auto domain    = IdentityText(identity.Domain, identity.DomainLength, identity.Flags);
  std::strncpy(event.connected.user, user.c_str(), sizeof(event.connected.user) - 1);
  std::strncpy(event.connected.domain, domain.c_str(), sizeof(event.connected.domain) - 1);
  event.connected.authenticated = client->authenticated != FALSE;
}
namespace {
bool NtlmResponseKey(AuthenticationState const& identity, BYTE* nt_hash_v1, BYTE* response) {
  // FreeRDP 3.15's NTLM callback consumes a response key and treats nonzero as success.
  auto user = TranscodeRange<std::vector<BYTE>>(
      std::as_bytes(std::span(identity.user)), {},
      { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little });
  auto domain = TranscodeRange<std::vector<BYTE>>(
      std::as_bytes(std::span(identity.domain)), {},
      { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little });
  auto user_length   = user.size();
  auto domain_length = domain.size();
  user.resize(user_length + sizeof(WCHAR));
  domain.resize(domain_length + sizeof(WCHAR));
  return NTOWFv2FromHashW(nt_hash_v1, reinterpret_cast<WCHAR*>(user.data()), user_length,
                          reinterpret_cast<WCHAR*>(domain.data()), domain_length, response);
}
bool ResponseKey(Peer& peer, SEC_WINNT_AUTH_IDENTITY const& identity, BYTE* response) {
  peer.authentication.user   = IdentityText(identity.User, identity.UserLength, identity.Flags);
  peer.authentication.domain = IdentityText(identity.Domain, identity.DomainLength, identity.Flags);
  auto const& config = peer.owner.authentication.Config();
  NtHash hash;
  bool const known = config.lookup ? config.lookup(config.auth_user, peer.authentication.domain.c_str(),
                                                   peer.authentication.user.c_str(), hash.Data()) != 0
                                   : sdlrdp_lookup_pair(&config, peer.authentication.domain.c_str(),
                                                        peer.authentication.user.c_str(), hash.Data()) != 0;
  return known && NtlmResponseKey(peer.authentication, hash.Data(), response);
}
}
SECURITY_STATUS AuthenticationHash(void* raw, SEC_WINNT_AUTH_IDENTITY const* identity, SecBuffer const* /*unused*/,
                                   BYTE const* /*unused*/, BYTE const* /*unused*/, SecBuffer const* /*unused*/,
                                   BYTE* response) {
  Expects(raw, "NTLM peer context exists");
  Expects(identity, "NTLM identity is supplied");
  Expects(response, "callback response is supplied");
  auto& peer = Peer::Held(static_cast<freerdp_peer*>(raw));
  peer.authentication.hash_attempted = true;
  try {
    bool const result = ResponseKey(peer, *identity, response);
    if (!result) Reject(peer);
    return result ? 1 : 0;
  } catch (...) {
    Reject(peer);
    return 0;
  }
}
void Peer::AuthenticationEnded() {
  if (authentication.hash_attempted && !authentication.checked) Reject(*this);
}
}
