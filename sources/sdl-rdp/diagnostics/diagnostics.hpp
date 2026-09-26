#pragma once
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <chrono>
#include <concepts>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::diagnostics::detail::diagnostics {
using sdl_rdp::utilities::Pinned;

class Diagnostics : private Pinned {
public:
       Diagnostics(LogSink& sink, bool tracing);
  auto Log(LogLevel level, std::string_view text) const                    -> void;
  auto Tracing() const noexcept                                            -> bool;
  auto Emit(std::string_view text) const                                   -> void;
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

private:
  LogRoute _route;
  bool     _tracing;
};
}

namespace sdl_rdp::diagnostics {
using detail::diagnostics::Diagnostics;
}
