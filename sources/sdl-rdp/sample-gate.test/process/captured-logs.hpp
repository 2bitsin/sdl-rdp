#pragma once
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <SDL3/SDL_log.h>
#include <optional>

namespace sdl_rdp::sample_gate_test::process::detail::captured_logs {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::utilities::Pinned;

struct Capture {
  bool                           forwarded = false;
  std::optional<SDL_LogPriority> video     = std::nullopt;
};

auto Level(SDL_LogPriority priority) -> LogLevel;

class CapturedLogs : private Pinned {
public:
  explicit CapturedLogs(Logs& logs, Capture capture = { });
           ~CapturedLogs();

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
using detail::captured_logs::Level;
}
