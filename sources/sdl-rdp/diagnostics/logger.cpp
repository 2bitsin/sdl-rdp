#include <sdl-rdp/diagnostics/logger.hpp>

namespace Backend {
Logger::Logger(sdlrdp_config const& config) : _route{ config }, _callback{ config.log }, _user{ config.log_user } { }
auto Logger::Log(sdlrdp_log_level level, std::string const& text) const -> void {
  if (_callback) _callback(_user, level, text.c_str());
}
}
