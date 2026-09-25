#pragma once
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>

#include <string_view>

namespace sdl_rdp::diagnostics::detail::logger {
class Logger {
public:
  explicit Logger(LogSink& sink);
  auto     Log(LogLevel level, std::string_view text) const -> void;

private:
  LogRoute _route;
};
}

namespace sdl_rdp::diagnostics {
using detail::logger::Logger;
}
