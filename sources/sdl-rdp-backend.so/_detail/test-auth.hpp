#pragma once
#include "../sdl-rdp-backend.h"
#include "auth-identity.hpp"
#include "headless-client.hpp"
#include "handle.hpp"
#include "peer.hpp"
#include "test-peer-status.hpp"

#include <condition_variable>
#include <format>
#include <gtest/gtest.h>
#include <mutex>
#include <oxbox/platform/scratch-area.hpp>
#include <thread>

namespace AuthenticationGate {
using BackendGate::CurrentStatus;
using BackendGate::RequiredStatus;
using utilities::Expects;
inline void ThenIdentity(sdlrdp_event const& event, char const* user, char const* domain, bool authenticated) {
  EXPECT_STREQ(event.connected.user, user);
  EXPECT_STREQ(event.connected.domain, domain);
  EXPECT_EQ(event.connected.authenticated, authenticated);
}
inline void ThenInformational(sdlrdp_log_level level, std::string const& text) {
  EXPECT_NE(level, SDLRDP_LOG_WARN) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
inline bool ReceiveIdentity(sdlrdp_handle* handle, char const* user, char const* domain, bool authenticated) {
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
inline void ThenSafeAuthenticationLog(sdlrdp_log_level level, std::string const& text, char const* password) {
  EXPECT_FALSE(text.contains(password));
  EXPECT_FALSE(text.contains("ERRBASE_SUCCESS")) << text;
  EXPECT_NE(level, SDLRDP_LOG_ERROR) << text;
}
class Authentication : public testing::Test {
protected:
  void TearDown() override { handle.reset(); }
  void Open(sdlrdp_auth mode, bool fixed = true) {
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
  static void Log(void* raw, sdlrdp_log_level level, char const* text) {
    auto& self = *static_cast<Authentication*>(raw);
    // WLog routing is process-wide; the headless client runs on the test thread.
    if (std::this_thread::get_id() == self.client_thread) return;
    std::scoped_lock const lock(self.guard);
    self.logs.emplace_back(level, text);
    self.logged.notify_all();
  }
  bool Until(auto ready) {
    std::unique_lock lock(guard);
    return logged.wait_for(lock, std::chrono::seconds(10), ready);
  }
  static int Verify(void* raw, char const* domain, char const* user, char const* password) {
    auto& self = *static_cast<Authentication*>(raw);
    self.order           += 'V';
    self.seen_domain     =  domain;
    self.seen_user       =  user;
    self.seen_password   =  password;
    self.callback_thread =  std::this_thread::get_id();
    return self.permit;
  }
  static int Lookup(void* raw, char const* domain, char const* user, unsigned char* hash) {
    auto& self = *static_cast<Authentication*>(raw);
    self.order += 'L';
    EXPECT_TRUE(self.seen_password.empty());
    return sdlrdp_lookup_pair(&self.config, domain, user, hash);
  }
  void Attempt(char const* user, char const* password, char const* domain, bool nla, bool accepted) {
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
  void PasswordCleared() {
    auto const  status   = RequiredStatus(*handle);
    auto const* password = freerdp_settings_get_string(status.client->context->settings, FreeRDP_Password);
    EXPECT_TRUE(!password || !*password);
  }
  void ThenRejection(sdlrdp_log_level level, std::string const& text, unsigned rejected) {
    EXPECT_EQ(level, SDLRDP_LOG_WARN);
    if (rejected < rejections.size()) EXPECT_EQ(text, rejections[rejected]);
  }
  void RejectionLogs(char const* password, unsigned expected = 1) {
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
  void ThenSecurityWarning(bool nla) {
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
  void ThenCertificateDisconnect(std::string_view closed) {
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
  void ThenPendingDisconnect(UINT32 code) {
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
  oxbox::platform::ScratchArea                            certificates    { "auth", "sdl-rdp"     };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle          { nullptr, sdlrdp_close };
  sdlrdp_config                                           config          { };
  std::mutex                                              guard;
  std::condition_variable                                 logged;
  std::vector<std::pair<sdlrdp_log_level, std::string>>   logs;
  std::string                                             order;
  std::string                                             seen_user;
  std::string                                             seen_domain;
  std::string                                             seen_password;
  std::vector<std::string>                                rejections;
  bool                                                    permit          = true;
  std::thread::id                                         callback_thread;
  std::thread::id                                         client_thread   = std::this_thread::get_id();
};
}
