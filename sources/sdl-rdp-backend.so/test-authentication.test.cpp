#include "_detail/test-authentication.hpp"

#include "_detail/auth-identity.hpp"
#include "_detail/client.hpp"
#include "_detail/handle.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <span>

namespace AuthenticationGate {
namespace {
void ThenIdentity(sdlrdp_event const& event, char const* user, char const* domain, bool authenticated) {
  EXPECT_STREQ(event.connected.user, user);
  EXPECT_STREQ(event.connected.domain, domain);
  EXPECT_EQ(event.connected.authenticated, authenticated);
}
void ThenInformational(sdlrdp_log_level level, std::string const& text) {
  EXPECT_NE(level, SDLRDP_LOG_WARN) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
bool ReceiveIdentity(sdlrdp_handle* handle, char const* user, char const* domain, bool authenticated) {
  std::array<sdlrdp_event, 32> events    { };
  bool                         connected = false;
  while (auto count = sdlrdp_poll(handle, events.data(), 32))
    for (auto const& event : std::span(events.data(), count)) {
      if (event.type != SDLRDP_CONNECTED) continue;
      connected = true;
      ThenIdentity(event, user, domain, authenticated);
    }
  return connected;
}
void ThenSafeAuthenticationLog(sdlrdp_log_level level, std::string const& text, char const* password) {
  EXPECT_FALSE(text.contains(password));
  EXPECT_FALSE(text.contains("ERRBASE_SUCCESS")) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
}
void Authentication::TearDown() {
  handle.reset();
}
void Authentication::Open(sdlrdp_auth mode, bool fixed) {
  auto directory = certificates.Path().string();
  config.bind      = "127.0.0.1";
  config.cert_dir  = directory.c_str();
  config.width     = 320;
  config.height    = 200;
  config.auth      = mode;
  config.log       = Log;
  config.log_user  = this;
  config.auth_user = this;
  if (fixed) {
    config.user     = "alice";
    config.password = "correct-secret";
    config.domain   = "LAB";
  }
  sdlrdp_handle* raw = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0) << sdlrdp_last_error();
  handle.reset(raw);
}
void Authentication::Log(void* raw, sdlrdp_log_level level, char const* text) {
  auto& self = *static_cast<Authentication*>(raw);
  // WLog routing is process-wide; the headless client runs on the test thread.
  if (std::this_thread::get_id() == self.client_thread) return;
  std::scoped_lock const lock(self.guard);
  self.logs.emplace_back(level, text);
  self.logged.notify_all();
}
int Authentication::Verify(void* raw, char const* domain, char const* user, char const* password) {
  auto& self = *static_cast<Authentication*>(raw);
  self.order           += 'V';
  self.seen_domain     =  domain;
  self.seen_user       =  user;
  self.seen_password   =  password;
  self.callback_thread =  std::this_thread::get_id();
  return self.permit;
}
int Authentication::Lookup(void* raw, char const* domain, char const* user, unsigned char* hash) {
  auto& self = *static_cast<Authentication*>(raw);
  self.order += 'L';
  EXPECT_TRUE(self.seen_password.empty());
  return sdlrdp_lookup_pair(&self.config, domain, user, hash);
}
void Authentication::Attempt(char const* user, char const* password, char const* domain, bool nla, bool accepted) {
  Headless::Client client(sdlrdp_port(handle.get()), false);
  client.Credentials(user, password, domain, nla);
  ASSERT_EQ(bool(freerdp_connect(client.Instance().get())), accepted);
  if (!accepted)
    rejections.push_back(
        std::format("Authentication rejected: user \"{}\" from 127.0.0.1", Backend::QualifiedName(domain, user)));
  bool connected = false;
  auto receive   = [&] {
    connected = ReceiveIdentity(handle.get(), user, domain, config.auth != SDLRDP_AUTH_NONE) || connected;
    return connected;
  };
  if (accepted)
    ASSERT_TRUE(client.Until(receive));
  else
    EXPECT_FALSE(receive());
  EXPECT_EQ(connected, accepted);
  if (connected) PasswordCleared();
}
void Authentication::PasswordCleared() {
  auto const  status   = RequiredStatus(*handle);
  auto const* password = freerdp_settings_get_string(status.client->context->settings, FreeRDP_Password);
  EXPECT_TRUE(!password || !*password);
}
void Authentication::ThenRejection(sdlrdp_log_level level, std::string const& text, unsigned rejected) {
  EXPECT_EQ(level, SDLRDP_LOG_WARN);
  if (rejected < rejections.size()) EXPECT_EQ(text, rejections[rejected]);
}
void Authentication::RejectionLogs(char const* password, unsigned expected) {
  handle.reset();
  std::scoped_lock const lock(guard);
  unsigned               rejected = 0;
  unsigned               warnings = 0;
  std::string            trace;
  for (auto const& [level, text] : logs) {
    warnings += level == SDLRDP_LOG_WARN;
    ThenSafeAuthenticationLog(level, text, password);
    if (text.starts_with("Authentication rejected:")) {
      ThenRejection(level, text, rejected);
      ++rejected;
      trace += text + '\n';
    }
  }
  EXPECT_EQ(rejected, expected);
  EXPECT_EQ(warnings, expected);
  RecordProperty("trace", trace);
}
void Authentication::ThenSecurityWarning(bool nla) {
  unsigned warnings = 0;
  for (auto const& [level, text] : logs) {
    EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
    if (level != SDLRDP_LOG_WARN) continue;
    ++warnings;
    EXPECT_EQ(text, nla ? "TLS handshake failed: client requested TLS|NLA, server selected TLS"
                        : "Connection refused: client requested RDP, server offers TLS");
  }
  EXPECT_EQ(warnings, 1);
}
void Authentication::ThenCertificateDisconnect(std::string_view closed) {
  unsigned    disconnects = 0;
  std::string trace;
  for (auto const& [level, text] : logs) {
    ThenInformational(level, text);
    trace += text + '\n';
    if (text != closed) continue;
    ++disconnects;
    EXPECT_EQ(level, SDLRDP_LOG_INFO);
  }
  EXPECT_EQ(disconnects, 1) << trace;
  RecordProperty("trace", trace);
}
void Authentication::ThenPendingDisconnect(UINT32 code) {
  auto const* message =
      code == FREERDP_ERROR_CONNECT_FAILED ? "Peer transport failed with pending data:" : "Peer disconnected:";
  EXPECT_TRUE(Until([&] {
    return std::ranges::any_of(logs, [&](auto const& entry) {
      auto const& [level, text] = entry;
      if (!text.starts_with(message)) return false;
      EXPECT_EQ(level, code == FREERDP_ERROR_CONNECT_FAILED ? SDLRDP_LOG_ERROR : SDLRDP_LOG_INFO) << text;
      return true;
    });
  }));
}
}
