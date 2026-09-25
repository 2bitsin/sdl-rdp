#include <sdl-rdp/diagnostics/logger.hpp>

namespace sdl_rdp::diagnostics::detail::logger {
Logger::Logger(sdlrdp_config const& config) : _route{ config } { }
auto Logger::Log(sdlrdp_log_level level, std::string const& text) const -> void {
  _route.Log(level, text);
}
}
