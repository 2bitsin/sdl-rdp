#pragma once
#include <sdl-rdp/core/error-store.hpp>
#include <sdl-rdp/core/logger.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

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
  auto Log(sdlrdp_log_level level, std::string const& text) const          -> void;
  auto Tracing() const noexcept                                            -> bool;
  auto Emit(std::string const& text) const                                 -> void;
  auto Line(std::string_view event, std::invocable auto&&... fields) const -> void {
    if (_tracing) Emit(Format(event, std::forward<decltype(fields)>(fields)...));
  }
  auto Format(std::string_view event, std::invocable auto&&... fields) const -> std::string {
    if (!_tracing) return { };
    auto const since = std::chrono::system_clock::now().time_since_epoch();
    auto const stamp = std::chrono::floor<std::chrono::milliseconds>(since).count();
    auto       text  = std::format("trace {} t={}", event, stamp);
    ((text += std::format(" {}", std::forward<decltype(fields)>(fields)())), ...);
    return text;
  }
  auto Fail(std::string text) -> void;

private:
  Logger     _logger;
  ErrorStore _errors;
  bool       _tracing;
};
}
