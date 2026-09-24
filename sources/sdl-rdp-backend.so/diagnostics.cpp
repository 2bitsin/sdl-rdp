#include "_detail/diagnostics.hpp"

namespace Backend {
Diagnostics::Diagnostics(sdlrdp_config const& config, bool tracing) : _logger{ config }, _tracing{ tracing } { }
void Diagnostics::Log(sdlrdp_log_level level, std::string const& text) const {
  _logger.Log(level, text);
}
bool Diagnostics::Tracing() const noexcept {
  return _tracing;
}
void Diagnostics::Emit(std::string const& text) const {
  _logger.Log(SDLRDP_LOG_INFO, text);
}
void Diagnostics::Fail(std::string text) {
  ErrorStore::Publish(&_errors, std::move(text));
}
}
