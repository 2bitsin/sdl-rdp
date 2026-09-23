#include "sdl-rdp-backend.h"
#include "_detail/headless-client.hpp"
#include "_detail/auth-identity.hpp"
#include "_detail/state.hpp"
#include <oxbox/platform/scratch-area.hpp>
#include <gtest/gtest.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <format>

namespace {
class Authentication : public testing::Test {
protected:
  oxbox::platform::ScratchArea certificates{"auth", "sdl-rdp"};
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle{nullptr, sdlrdp_close};
  sdlrdp_config config{};
  std::mutex guard;
  std::condition_variable logged;
  std::vector<std::pair<sdlrdp_log_level, std::string>> logs;
  std::string order, seen_user, seen_domain, seen_password;
  std::vector<std::string> rejections;
  bool permit = true;
  std::thread::id callback_thread;
  std::thread::id client_thread = std::this_thread::get_id();
  void TearDown() override { handle.reset(); }
  void Open(sdlrdp_auth mode, bool fixed = true) {
    auto directory = certificates.Path().string();
    config.bind = "127.0.0.1"; config.cert_dir = directory.c_str();
    config.width = 320; config.height = 200; config.auth = mode;
    config.log = Log; config.log_user = this; config.auth_user = this;
    if (fixed) { config.user = "alice"; config.password = "correct-secret"; config.domain = "LAB"; }
    sdlrdp_handle* raw = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &raw), 0) << sdlrdp_last_error();
    handle.reset(raw);
  }
  static void Log(void* raw, sdlrdp_log_level level, char const* text) {
    auto& self = *static_cast<Authentication*>(raw);
    // WLog routing is process-wide; the headless client runs on the test thread.
    if (std::this_thread::get_id() == self.client_thread) return;
    std::scoped_lock lock(self.guard);
    self.logs.emplace_back(level, text);
    self.logged.notify_all();
  }
  bool Until(auto ready) {
    std::unique_lock lock(guard);
    return logged.wait_for(lock, std::chrono::seconds(10), ready);
  }
  static int Verify(void* raw, char const* domain, char const* user, char const* password) {
    auto& self = *static_cast<Authentication*>(raw);
    self.order += 'V'; self.seen_domain = domain; self.seen_user = user; self.seen_password = password;
    self.callback_thread = std::this_thread::get_id();
    return self.permit;
  }
  static int Lookup(void* raw, char const* domain, char const* user, unsigned char hash[16]) {
    auto& self = *static_cast<Authentication*>(raw);
    self.order += 'L';
    EXPECT_TRUE(self.seen_password.empty());
    return sdlrdp_lookup_pair(&self.config, domain, user, hash);
  }
  void Attempt(char const* user, char const* password, char const* domain, bool nla, bool accepted) {
    Headless::Client client(sdlrdp_port(handle.get()), false);
    client.Credentials(user, password, domain, nla);
    ASSERT_EQ(bool(freerdp_connect(client.instance.get())), accepted);
    if (!accepted) rejections.push_back(std::format(
      "Authentication rejected: user \"{}\" from 127.0.0.1", Backend::QualifiedName(domain, user)));
    sdlrdp_event events[32];
    bool connected = false;
    auto receive = [&] {
      while (auto count = sdlrdp_poll(handle.get(), events, 32)) for (auto const& event : std::span(events, count)) {
        if (event.type != SDLRDP_CONNECTED) continue;
        connected = true;
        EXPECT_STREQ(event.connected.user, user);
        EXPECT_STREQ(event.connected.domain, domain);
        EXPECT_EQ(event.connected.authenticated, config.auth != SDLRDP_AUTH_NONE);
      }
      return connected;
    };
    if (accepted) ASSERT_TRUE(client.Until(receive));
    else EXPECT_FALSE(receive());
    EXPECT_EQ(connected, accepted);
    if (connected) PasswordCleared();
  }
  void PasswordCleared() {
    auto& state = *handle->state;
    std::scoped_lock lock(state.session_guard);
    ASSERT_NE(state.current, nullptr);
    auto password = freerdp_settings_get_string(state.current->client->context->settings, FreeRDP_Password);
    EXPECT_TRUE(!password || !*password);
  }
  void RejectionLogs(char const* password, unsigned expected = 1) {
    handle.reset();
    std::scoped_lock lock(guard);
    unsigned rejected = 0, warnings = 0;
    std::string trace;
    for (auto const& [level, text] : logs) {
      warnings += level == SDLRDP_LOG_WARN;
      EXPECT_FALSE(text.contains(password));
      EXPECT_FALSE(text.contains("ERRBASE_SUCCESS")) << text;
      EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
      if (text.starts_with("Authentication rejected:")) {
        EXPECT_EQ(level, SDLRDP_LOG_WARN);
        if (rejected < rejections.size()) EXPECT_EQ(text, rejections[rejected]);
        ++rejected;
        trace += text + '\n';
      }
    }
    EXPECT_EQ(rejected, expected);
    EXPECT_EQ(warnings, expected);
    RecordProperty("trace", trace);
  }
};
TEST_F(Authentication, NoneReportsIdentity) {
  Open(SDLRDP_AUTH_NONE, false);
  Attempt("žąsis", "irrelevant-secret", "LAB", false, true);
  RecordProperty("trace", "none: user=žąsis domain=LAB authenticated=0");
}
TEST_F(Authentication, TlsFixedPair) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "correct-secret", "LAB", false, true);
  RecordProperty("trace", "tls fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, DnsDomain) {
  std::string domain = std::string(63, 'a') + "." + std::string(63, 'b') + ".example.org";
  config.domain = domain.c_str(); config.user = "alice"; config.password = "correct-secret";
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "correct-secret", domain.c_str(), false, true);
}
TEST_F(Authentication, WrongPassword) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "wrong-secret", "LAB", false, false);
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, WrongUser) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("mallory", "correct-secret", "LAB", false, false);
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, WrongDomain) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "correct-secret", "OTHER", false, false);
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, UnsetDomain) {
  config.user = "alice"; config.password = "correct-secret";
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "correct-secret", "OTHER", false, true);
  RecordProperty("trace", "tls fixed pair: unset domain admits OTHER; authenticated=1");
}
TEST_F(Authentication, VerifyDecides) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("žąsis", "callback-secret", "ŽEMĖ", false, true);
  EXPECT_EQ(seen_user, "žąsis"); EXPECT_EQ(seen_domain, "ŽEMĖ");
  EXPECT_TRUE(seen_password == "callback-secret");
  EXPECT_NE(callback_thread, std::this_thread::get_id());
  permit = false;
  Attempt("žąsis", "callback-secret", "ŽEMĖ", false, false);
  RejectionLogs("callback-secret");
}
TEST_F(Authentication, MissingVerify) {
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "missing-secret", "LAB", false, false);
  RejectionLogs("missing-secret");
}
TEST_F(Authentication, NlaFixedPair) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, true);
  RecordProperty("trace", "nla fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, NlaUnicode) {
  config.user = "žąsis"; config.password = "unicode-secret"; config.domain = "ŽEMĖ";
  Open(SDLRDP_AUTH_NLA, false);
  Attempt("žąsis", "unicode-secret", "ŽEMĖ", true, true);
  RecordProperty("trace", "nla UTF-8 user=žąsis domain=ŽEMĖ authenticated=1");
}
TEST_F(Authentication, NlaVerifyDenies) {
  config.verify = Verify; config.lookup = Lookup; permit = false;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, false);
  handle.reset(); EXPECT_EQ(order, "LV");
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, NlaWrongPassword) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "wrong-secret", "LAB", true, false);
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaHashBeforePassword) {
  config.verify = Verify; config.lookup = Lookup;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "wrong-secret", "LAB", true, false);
  handle.reset();
  EXPECT_EQ(order, "L"); EXPECT_TRUE(seen_password.empty());
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaCallbackOrder) {
  config.verify = Verify; config.lookup = Lookup;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, true);
  handle.reset();
  EXPECT_EQ(order, "LV"); EXPECT_TRUE(seen_password == "correct-secret");
  RecordProperty("trace", "nla: lookup -> hash check -> verify; authenticated=1");
}
TEST_F(Authentication, NlaAcceptsTls) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", false, true);
  handle.reset(); EXPECT_EQ(order, "V");
  RecordProperty("trace", "nla server + tls client: verify only; authenticated=1");
}
TEST_F(Authentication, NlaMissingLookup) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_NLA, false);
  Attempt("alice", "missing-secret", "LAB", true, false);
  EXPECT_TRUE(order.empty());
  Attempt("alice", "missing-secret", "LAB", false, true);
  handle.reset(); EXPECT_EQ(order, "V");
  RejectionLogs("missing-secret");
}
TEST_F(Authentication, RefusedSecurityLogs) {
  for (bool nla : {true, false}) {
    Open(SDLRDP_AUTH_TLS);
    {
      Headless::Client client(sdlrdp_port(handle.get()), false);
      client.Credentials("alice", "correct-secret", "LAB", nla);
      auto settings = client.instance->context->settings;
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE));
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, !nla));
      EXPECT_FALSE(freerdp_connect(client.instance.get()));
    }
    handle.reset();
    unsigned warnings = 0;
    for (auto const& [level, text] : logs) {
      EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
      if (level != SDLRDP_LOG_WARN) continue;
      ++warnings;
      EXPECT_EQ(text, nla ? "TLS handshake failed: client requested TLS|NLA, server selected TLS"
                         : "Connection refused: client requested RDP, server offers TLS");
    }
    EXPECT_EQ(warnings, 1);
    logs.clear();
  }
}
TEST_F(Authentication, RejectedCertificateLogs) {
  Open(SDLRDP_AUTH_TLS);
  static thread_local bool verified;
  verified = false;
  {
    Headless::Client client(sdlrdp_port(handle.get()), false);
    client.Credentials("alice", "correct-secret", "LAB", true);
    auto settings = client.instance->context->settings;
    ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE));
    ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, FALSE));
    client.instance->VerifyCertificateEx = [](freerdp*, char const*, UINT16, char const*,
        char const*, char const*, char const*, DWORD) -> DWORD {
      verified = true;
      return 0;
    };
    EXPECT_FALSE(freerdp_connect(client.instance.get()));
  }
  constexpr auto closed = "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
  EXPECT_TRUE(Until([&] {
    return std::ranges::any_of(logs, [&](auto const& entry) { return entry.second == closed; });
  }));
  handle.reset();
  EXPECT_TRUE(verified);
  unsigned disconnects = 0;
  std::string trace;
  for (auto const& [level, text] : logs) {
    EXPECT_NE(level, SDLRDP_LOG_WARN) << text;
    EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
    trace += text + '\n';
    if (text != closed) continue;
    ++disconnects;
    EXPECT_EQ(level, SDLRDP_LOG_INFO);
  }
  EXPECT_EQ(disconnects, 1) << trace;
  RecordProperty("trace", trace);
}
TEST_F(Authentication, NlaUnknownIdentities) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("bob", "correct-secret", "LAB", true, false);
  Attempt("alice", "correct-secret", "OTHER", true, false);
  Attempt("alice", "correct-secret", "", true, false);
  RejectionLogs("correct-secret", 3);
}
TEST_F(Authentication, PendingDisconnectLogLevels) {
  for (auto code : {FREERDP_ERROR_CONNECT_TRANSPORT_FAILED, FREERDP_ERROR_LOGOFF_BY_USER,
                    FREERDP_ERROR_CONNECT_FAILED}) {
    Open(SDLRDP_AUTH_TLS);
    Headless::Client client(sdlrdp_port(handle.get()), false);
    client.Credentials("alice", "correct-secret", "LAB");
    ASSERT_TRUE(freerdp_connect(client.instance.get()));
    auto& state = *handle->state;
    ASSERT_TRUE(client.Until([&] {
      std::scoped_lock lock(state.session_guard);
      return state.current != nullptr;
    }));
    {
      std::scoped_lock lock(state.session_guard);
      auto& peer = *state.current;
      { std::scoped_lock frame(state.frame_guard); peer.dirty.Add({0, 0, 1, 1}); }
      freerdp_set_last_error(peer.client->context, code);
      peer.client->CheckFileDescriptor = [](freerdp_peer*) -> BOOL { return FALSE; };
      SetEvent(peer.wake.get());
    }
    auto message = code == FREERDP_ERROR_CONNECT_FAILED ? "Peer transport failed with pending data:"
                                                       : "Peer disconnected:";
    EXPECT_TRUE(Until([&] {
      for (auto const& [level, text] : logs) if (text.starts_with(message)) {
        EXPECT_EQ(level, code == FREERDP_ERROR_CONNECT_FAILED ? SDLRDP_LOG_ERROR : SDLRDP_LOG_INFO) << text;
        return true;
      }
      return false;
    }));
    handle.reset();
    logs.clear();
  }
}
TEST_F(Authentication, TenRejectionsThenSuccess) {
  Open(SDLRDP_AUTH_TLS);
  for (unsigned i = 0; i < 10; ++i) Attempt("alice", "wrong-secret", "LAB", false, false);
  Attempt("alice", "correct-secret", "LAB", false, true);
  RejectionLogs("wrong-secret", 10);
}
TEST(AuthenticationIdentity, UnicodeAndAnsi) {
  UINT16 unicode[] = {0x017e, 0x0105, 's', 'i', 's'};
  EXPECT_EQ(Backend::IdentityText(unicode, 5, SEC_WINNT_AUTH_IDENTITY_UNICODE), "žąsis");
  char ansi[] = "Aé";
  EXPECT_EQ(Backend::IdentityText(reinterpret_cast<UINT16*>(ansi), sizeof(ansi) - 1, SEC_WINNT_AUTH_IDENTITY_ANSI), "Aé");
}
}
