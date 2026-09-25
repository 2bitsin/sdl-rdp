#include <sdl-rdp/diagnostics/logger.hpp>

namespace sdl_rdp::diagnostics::detail::logger {
Logger::Logger(LogSink& sink) : _route{ sink } { }
auto Logger::Log(LogLevel level, std::string_view text) const -> void {
  _route.Log(level, text);
}
}
