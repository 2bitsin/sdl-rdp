#pragma once
#include "instance.hpp"
#include "status.hpp"
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/contract.hpp>

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
using sdl_rdp::utilities::Expects;
struct FixedPair {
  std::string                user;
  std::string                password;
  std::optional<std::string> domain;
};
struct BackendSetup {
  sdlrdp_auth              mode     = SDLRDP_AUTH_NONE;
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
class Authentication : public testing::Test {
protected:
  auto        TearDown()                                               -> void override;
  auto        Open(sdlrdp_auth chosen, bool fixed = true)              -> void;
  static auto Log(void* raw, sdlrdp_log_level level, char const* text) -> void;
  auto        Until(auto ready)                                        -> bool {
    std::unique_lock lock(guard);
    return logged.wait_for(lock, std::chrono::seconds(10), ready);
  }
  static auto Verify(void* raw, char const* domain, char const* user, char const* password)        -> int;
  static auto Lookup(void* raw, char const* domain, char const* user, std::uint8_t* hash)          -> int;
  auto Attempt(std::string_view user, std::string_view password, std::string_view domain, bool nla, bool accepted)
      -> void;
  auto        PasswordCleared()                                                                    -> void;
  auto        ThenRejection(sdlrdp_log_level level, std::string const& text, std::size_t rejected) -> void;
  auto        RejectionLogs(std::string_view password, std::size_t expected = 1)                   -> void;
  auto        ThenSecurityWarning(bool nla)                                                        -> void;
  auto        ThenCertificateDisconnect(std::string_view closed)                                   -> void;
  auto        ThenPendingDisconnect(std::uint32_t code)                                            -> void;
  auto        Config()                                                                             -> sdlrdp_config;
  oxbox::platform::ScratchArea                          certificates  { "auth", "sdl-rdp" };
  BackendInstance                                       handle;
  BackendSetup                                          setup;
  std::mutex                                            guard;
  std::condition_variable                               logged;
  std::vector<std::pair<sdlrdp_log_level, std::string>> logs;
  CallbackRecord                                        seen;
  std::vector<std::string>                              rejections;
  bool                                                  permit        = true;
  std::thread::id                                       client_thread = std::this_thread::get_id();
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::authentication::Authentication;
using detail::authentication::FixedPair;
}
