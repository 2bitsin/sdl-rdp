#pragma once
#include "instance.hpp"
#include "status.hpp"
#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace sdl_rdp::headless_client_test::backend::detail::authentication {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::diagnostics::LogSink;
using sdl_rdp::utilities::NtOwf;
struct FixedPair {
  std::string                user;
  std::string                password;
  std::optional<std::string> domain;
};
struct BackendSetup {
  AuthMode                 mode     = AuthMode::None;
  std::optional<FixedPair> pair;
  bool                     verifies = false;
  bool                     looks_up = false;
};
struct CallbackRecord {
  std::string     order;
  std::string     user;
  std::string     domain;
  std::string     password;
  std::thread::id thread;
};
// The fixture is the backend's log sink, and its credential check when a test records the callbacks: Verifies
// answers permit, NtHash answers the fixed pair's hash. Otherwise BackendInstance's account answers.
class Authentication : public testing::Test, public LogSink, public CredentialCheck {
public:
  auto Log(LogLevel level, std::string_view text)                   -> void                 override;
  auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool override;
  auto NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> override;

protected:
  auto TearDown()                               -> void override;
  auto Open(AuthMode chosen, bool fixed = true) -> void;
  auto Until(auto ready)                        -> bool {
    std::unique_lock lock(guard);
    return logged.wait_for(lock, std::chrono::seconds(10), ready);
  }
  auto Attempt(std::string_view user, std::string_view password, std::string_view domain, bool nla, bool accepted)
      -> void;
  auto PasswordCleared()                                                            -> void;
  auto ThenRejection(LogLevel level, std::string const& text, std::size_t rejected) -> void;
  auto RejectionLogs(std::string_view password, std::size_t expected = 1)           -> void;
  auto ThenSecurityWarning(bool nla)                                                -> void;
  auto ThenCertificateDisconnect(std::string_view closed)                           -> void;
  auto ThenPendingDisconnect(std::uint32_t code)                                    -> void;
  auto Config() const                                                               -> sdl_rdp::configuration::Setup;
  oxbox::platform::ScratchArea                  certificates  { "auth", "sdl-rdp" };
  BackendInstance                               backend;
  BackendSetup                                  setup;
  std::mutex                                    guard;
  std::condition_variable                       logged;
  std::vector<std::pair<LogLevel, std::string>> logs;
  mutable CallbackRecord                        seen;
  std::vector<std::string>                      rejections;
  bool                                          permit        = true;
  std::thread::id                               client_thread = std::this_thread::get_id();
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::authentication::Authentication;
using detail::authentication::FixedPair;
}
