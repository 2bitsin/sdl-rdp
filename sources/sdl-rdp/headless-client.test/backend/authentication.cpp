#include <sdl-rdp/headless-client.test/backend/authentication.hpp>
#include <sdl-rdp/abi/backend.h>

#include <sdl-rdp/auth/identity.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/session/handle.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace AuthenticationGate {
namespace {
struct Identity {
  std::string_view user;
  std::string_view domain;
  bool             authenticated;
};
auto ThenIdentity(sdlrdp_event const& event, Identity expected) -> void {
  EXPECT_EQ(std::string_view(event.connected.user), expected.user);
  EXPECT_EQ(std::string_view(event.connected.domain), expected.domain);
  EXPECT_EQ(event.connected.authenticated, static_cast<int>(expected.authenticated));
}
auto ThenInformational(sdlrdp_log_level level, std::string const& text) -> void {
  EXPECT_NE(level, SDLRDP_LOG_WARN) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
auto AnyConnectedIdentity(std::span<sdlrdp_event const> events, Identity expected) -> bool {
  auto connections = events | std::views::filter([](auto const& event) { return event.type == SDLRDP_CONNECTED; });
  std::ranges::for_each(connections, [=](auto const& event) { ThenIdentity(event, expected); });
  return !std::ranges::empty(connections);
}
auto ReceiveIdentity(sdlrdp_handle& handle, Identity expected) -> bool {
  std::array<sdlrdp_event, 32> events    { };
  bool                         connected = false;
  while (auto count = sdlrdp_poll(&handle, events.data(), events.size()))
    connected = AnyConnectedIdentity(std::span(events.data(), count), expected) || connected;
  return connected;
}
auto ThenSafeAuthenticationLog(sdlrdp_log_level level, std::string const& text, char const* password) -> void {
  EXPECT_FALSE(text.contains(password));
  EXPECT_FALSE(text.contains("ERRBASE_SUCCESS")) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
}
auto Authentication::TearDown() -> void {
  handle.Close();
}
auto Authentication::Open(sdlrdp_auth mode, bool fixed) -> void {
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
  ASSERT_NO_FATAL_FAILURE(handle.Open(config));
}
auto Authentication::Log(void* raw, sdlrdp_log_level level, char const* text) -> void {
  auto& self = *static_cast<Authentication*>(raw);
  // WLog routing is process-wide; the headless client runs on the test thread.
  if (std::this_thread::get_id() == self.client_thread) return;
  std::scoped_lock const lock(self.guard);
  self.logs.emplace_back(level, text);
  self.logged.notify_all();
}
auto Authentication::Verify(void* raw, char const* domain, char const* user, char const* password) -> int {
  auto& self = *static_cast<Authentication*>(raw);
  self.seen.order    += 'V';
  self.seen.domain   =  domain;
  self.seen.user     =  user;
  self.seen.password =  password;
  self.seen.thread   =  std::this_thread::get_id();
  return self.permit;
}
auto Authentication::Lookup(void* raw, char const* domain, char const* user, std::uint8_t* hash) -> int {
  auto& self = *static_cast<Authentication*>(raw);
  self.seen.order += 'L';
  EXPECT_TRUE(self.seen.password.empty());
  return sdlrdp_lookup_pair(&self.config, domain, user, hash);
}
auto Authentication::Attempt(char const* user, char const* password, char const* domain, bool nla, bool accepted)
    -> void {
  Headless::Client client(sdlrdp_port(handle.Handle()), false);
  client.Credentials(user, password, domain, nla);
  ASSERT_EQ(client.Connect(), accepted);
  if (!accepted)
    rejections.push_back(
        std::format("Authentication rejected: user \"{}\" from 127.0.0.1", Backend::QualifiedName(domain, user)));
  Identity const expected  { .user = user, .domain = domain, .authenticated = config.auth != SDLRDP_AUTH_NONE };
  bool           connected = false;
  auto           receive   = [&] {
    connected = ReceiveIdentity(*handle, expected) || connected;
    return connected;
  };
  if (accepted)
    ASSERT_TRUE(client.Until(receive));
  else
    EXPECT_FALSE(receive());
  EXPECT_EQ(connected, accepted);
  if (connected) PasswordCleared();
}
auto Authentication::PasswordCleared() -> void {
  auto const  status   = RequiredStatus(*handle);
  auto const* password = freerdp_settings_get_string(status.client.get().context->settings, FreeRDP_Password);
  EXPECT_TRUE(!password || !*password);
}
auto Authentication::ThenRejection(sdlrdp_log_level level, std::string const& text, std::size_t rejected) -> void {
  EXPECT_EQ(level, SDLRDP_LOG_WARN);
  if (rejected < rejections.size()) EXPECT_EQ(text, rejections[rejected]);
}
auto Authentication::RejectionLogs(char const* password, std::size_t expected) -> void {
  handle.Close();
  std::scoped_lock const lock(guard);
  std::size_t            rejected = 0;
  std::size_t            warnings = 0;
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
auto Authentication::ThenSecurityWarning(bool nla) -> void {
  using Warnings = std::vector<std::string>;
  // FreeRDP 3.32 nego.c:682 retries a refused NLA_EXT attempt (nego.c:660) as NLA (nego.c:701).
  auto const expected = nla ? Warnings{ "TLS handshake failed: client requested TLS|NLA|NLA_EXT, server selected TLS",
                                        "TLS handshake failed: client requested TLS|NLA, server selected TLS" }
                            : Warnings{ "Connection refused: client requested RDP, server offers TLS" };
  Warnings   warnings;
  for (auto const& [level, text] : logs) {
    EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
    if (level == SDLRDP_LOG_WARN) warnings.push_back(text);
  }
  EXPECT_EQ(warnings, expected);
}
auto Authentication::ThenCertificateDisconnect(std::string_view closed) -> void {
  std::size_t disconnects = 0;
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
auto Authentication::ThenPendingDisconnect(std::uint32_t code) -> void {
  auto const* message = code == FREERDP_ERROR_CONNECT_FAILED ? "Peer transport failed with pending data:"
                                                             : "Peer disconnected:";
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
