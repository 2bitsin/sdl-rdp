#include <sdl-rdp/headless-client.test/backend/authentication.hpp>

#include <sdl-rdp/auth/identity.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace sdl_rdp::headless_client_test::backend::detail::authentication {
using sdl_rdp::auth::Account;
using sdl_rdp::auth::QualifiedName;
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::SettingsView;
using sdl_rdp::freerdp_facade::StringKey;
using sdl_rdp::headless_client_test::backend::EventsOf;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::link::Connected;
using sdl_rdp::session::Backend;
using sdl_rdp::utilities::WipedString;

namespace {
struct Identity {
  std::string_view user;
  std::string_view domain;
  bool             authenticated;
};
auto ThenIdentity(Connected const& event, Identity expected) -> void {
  EXPECT_EQ(event.user, expected.user);
  EXPECT_EQ(event.domain, expected.domain);
  EXPECT_EQ(event.authenticated, expected.authenticated);
}
auto ThenInformational(LogLevel level, std::string const& text) -> void {
  EXPECT_NE(level, LogLevel::Warn) << text;
  EXPECT_NE(level, LogLevel::Error) << text;
}
auto ReceiveIdentity(Backend& backend, Identity expected) -> bool {
  bool connected = false;
  for (auto events = backend.Events().Poll(); !events.empty(); events = backend.Events().Poll())
    for (auto const& event : EventsOf<Connected>(events)) {
      ThenIdentity(event, expected);
      connected = true;
    }
  return connected;
}
auto ThenSafeAuthenticationLog(LogLevel level, std::string const& text, std::string_view password) -> void {
  EXPECT_FALSE(text.contains(password));
  EXPECT_FALSE(text.contains("ERRBASE_SUCCESS")) << text;
  EXPECT_NE(level, LogLevel::Error) << text;
}
}
auto Authentication::TearDown() -> void {
  backend.Close();
}
auto Authentication::Open(AuthMode chosen, bool fixed) -> void {
  setup.mode = chosen;
  if (fixed) setup.pair = FixedPair{ .user = "alice", .password = "correct-secret", .domain = "LAB" };
  if (setup.verifies || setup.looks_up)
    ASSERT_NO_FATAL_FAILURE(backend.Open(Config(), *this, *this));
  else
    ASSERT_NO_FATAL_FAILURE(backend.Open(Config(), *this));
}
auto Authentication::Config() const -> sdl_rdp::configuration::Setup {
  auto config = LoopbackConfig(certificates.Path());
  config.auth = setup.mode;
  if (!setup.pair) return config;
  auto const& pair = *setup.pair;
  config.user     = pair.user;
  config.password = WipedString{ pair.password };
  config.domain   = pair.domain;
  return config;
}
auto Authentication::Log(LogLevel level, std::string_view text) -> void {
  // WLog routing is process-wide; the headless client runs on the test thread.
  if (std::this_thread::get_id() == client_thread) return;
  std::scoped_lock const lock(guard);
  logs.emplace_back(level, text);
  logged.notify_all();
}
auto Authentication::Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool {
  seen.order    += 'V';
  seen.domain   =  domain;
  seen.user     =  user;
  seen.password =  password;
  seen.thread   =  std::this_thread::get_id();
  return permit;
}
// With no lookup published the driver's relay falls back to the account, and so does the fixture.
auto Authentication::NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> {
  if (setup.looks_up) {
    seen.order += 'L';
    EXPECT_TRUE(seen.password.empty());
  }
  return Account{ Config() }.NtHash(domain, user);
}
auto Authentication::Attempt(std::string_view user, std::string_view password, std::string_view domain, bool nla,
                             bool accepted) -> void {
  Client client(backend.Port(), false);
  client.Credentials({ .user = user, .password = password, .domain = domain }, nla);
  ASSERT_EQ(client.Connect(), accepted);
  if (!accepted)
    rejections.push_back(
        std::format("Authentication rejected: user \"{}\" from 127.0.0.1", QualifiedName(domain, user)));
  Identity const expected  { .user = user, .domain = domain, .authenticated = setup.mode != AuthMode::None };
  bool           connected = false;
  auto           receive   = [&] {
    connected = ReceiveIdentity(*backend, expected) || connected;
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
  auto const status   = RequiredStatus(*backend);
  auto const password = SettingsView{ *status.client.get().context->settings }.Get(StringKey::Password);
  EXPECT_TRUE(!password || password->empty());
}
auto Authentication::ThenRejection(LogLevel level, std::string const& text, std::size_t rejected) -> void {
  EXPECT_EQ(level, LogLevel::Warn);
  if (rejected < rejections.size()) EXPECT_EQ(text, rejections[rejected]);
}
auto Authentication::RejectionLogs(std::string_view password, std::size_t expected) -> void {
  backend.Close();
  std::scoped_lock const lock(guard);
  std::size_t            rejected = 0;
  std::size_t            warnings = 0;
  std::string            trace;
  for (auto const& [level, text] : logs) {
    warnings += level == LogLevel::Warn;
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
    EXPECT_NE(level, LogLevel::Error) << text;
    if (level == LogLevel::Warn) warnings.push_back(text);
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
    EXPECT_EQ(level, LogLevel::Info);
  }
  EXPECT_EQ(disconnects, 1) << trace;
  RecordProperty("trace", trace);
}
auto Authentication::ThenPendingDisconnect(std::uint32_t code) -> void {
  auto const* message = code == FREERDP_ERROR_CONNECT_FAILED ? "Peer transport failed with pending data:"
                                                             : "Peer disconnected:";
  EXPECT_TRUE(Logged([&](auto const& entry) {
    auto const& [level, text] = entry;
    if (!text.starts_with(message)) return false;
    EXPECT_EQ(level, code == FREERDP_ERROR_CONNECT_FAILED ? LogLevel::Error : LogLevel::Info) << text;
    return true;
  }));
}
}
