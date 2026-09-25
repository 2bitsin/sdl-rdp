#pragma once
#include <sdl-rdp/headless-client.test/backend/logs.hpp>

#include <SDL3/SDL_log.h>
#include <optional>

namespace sdl_rdp::sample_gate_test::process::detail::captured_logs {
using sdl_rdp::headless_client_test::backend::Logs;

struct Capture {
  bool                           forwarded = false;
  std::optional<SDL_LogPriority> video     = std::nullopt;
};

auto LogLevel(SDL_LogPriority priority) -> sdlrdp_log_level;

class CapturedLogs {
public:
  explicit CapturedLogs(Logs& logs, Capture capture = { });
           CapturedLogs(CapturedLogs const&)               = delete;
           CapturedLogs(CapturedLogs&&)                    = delete;
           ~CapturedLogs();
  auto     operator=(CapturedLogs const&) -> CapturedLogs& = delete;
  auto     operator=(CapturedLogs&&)      -> CapturedLogs& = delete;

private:
  // abi: SDL_LogOutputFunction
  static auto Collect(void* user, int category, SDL_LogPriority priority, char const* text) -> void;
  Logs&                          logs;
  bool                           forwarded;
  SDL_LogOutputFunction          previous      = nullptr;
  void*                          previous_user = nullptr;
  std::optional<SDL_LogPriority> video;
};
}

namespace sdl_rdp::sample_gate_test::process {
using detail::captured_logs::Capture;
using detail::captured_logs::CapturedLogs;
using detail::captured_logs::LogLevel;
}
