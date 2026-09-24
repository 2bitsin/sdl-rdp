#pragma once
#include "error-store.hpp"
#include "logger.hpp"
#include "pinned.hpp"
#include "sdl-rdp-backend.h"

#include <chrono>
#include <concepts>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Backend {
class Diagnostics : private Pinned {
public:
       Diagnostics(sdlrdp_config const& config, bool tracing);
  void Log(sdlrdp_log_level level, std::string const& text) const;
  bool Tracing() const noexcept;
  void Emit(std::string const& text) const;
  void Line(std::string_view event, std::invocable auto&&... fields) const {
    if (_tracing) Emit(Format(event, std::forward<decltype(fields)>(fields)...));
  }
  std::string Format(std::string_view event, std::invocable auto&&... fields) const {
    if (!_tracing) return { };
    auto const since = std::chrono::system_clock::now().time_since_epoch();
    auto const stamp = std::chrono::floor<std::chrono::milliseconds>(since).count();
    auto       text  = std::format("trace {} t={}", event, stamp);
    ((text += std::format(" {}", std::forward<decltype(fields)>(fields)())), ...);
    return text;
  }
  void Fail(std::string text);

private:
  Logger     _logger;
  ErrorStore _errors;
  bool       _tracing;
};
}
