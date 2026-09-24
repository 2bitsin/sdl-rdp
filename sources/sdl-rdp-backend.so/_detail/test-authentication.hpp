#pragma once
#include "contract.hpp"
#include "sdl-rdp-backend.h"
#include "test-peer-status.hpp"

#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <oxbox/platform/scratch-area.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <winpr/wtypes.h>

namespace AuthenticationGate {
using BackendGate::CurrentStatus;
using BackendGate::RequiredStatus;
using utilities::Expects;
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
  auto        Open(sdlrdp_auth mode, bool fixed = true)                -> void;
  static auto Log(void* raw, sdlrdp_log_level level, char const* text) -> void;
  auto        Until(auto ready)                                        -> bool {
    std::unique_lock lock(guard);
    return logged.wait_for(lock, std::chrono::seconds(10), ready);
  }
  static auto Verify(void* raw, char const* domain, char const* user, char const* password)                -> int;
  static auto Lookup(void* raw, char const* domain, char const* user, unsigned char* hash)                 -> int;
  auto        Attempt(char const* user, char const* password, char const* domain, bool nla, bool accepted) -> void;
  auto        PasswordCleared()                                                                            -> void;
  auto        ThenRejection(sdlrdp_log_level level, std::string const& text, unsigned rejected)            -> void;
  auto        RejectionLogs(char const* password, unsigned expected = 1)                                   -> void;
  auto        ThenSecurityWarning(bool nla)                                                                -> void;
  auto        ThenCertificateDisconnect(std::string_view closed)                                           -> void;
  auto        ThenPendingDisconnect(UINT32 code)                                                           -> void;
  oxbox::platform::ScratchArea                            certificates  { "auth", "sdl-rdp"     };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle        { nullptr, sdlrdp_close };
  sdlrdp_config                                           config        { };
  std::mutex                                              guard;
  std::condition_variable                                 logged;
  std::vector<std::pair<sdlrdp_log_level, std::string>>   logs;
  CallbackRecord                                          seen;
  std::vector<std::string>                                rejections;
  bool                                                    permit        = true;
  std::thread::id                                         client_thread = std::this_thread::get_id();
};
}
