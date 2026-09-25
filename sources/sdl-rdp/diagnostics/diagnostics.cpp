#include <sdl-rdp/diagnostics/diagnostics.hpp>

namespace sdl_rdp::diagnostics::detail::diagnostics {
Diagnostics::Diagnostics(sdlrdp_config const& config, bool tracing) : _logger{ config }, _tracing{ tracing } { }
auto Diagnostics::Log(sdlrdp_log_level level, std::string const& text) const -> void {
  _logger.Log(level, text);
}
auto Diagnostics::Tracing() const noexcept -> bool {
  return _tracing;
}
auto Diagnostics::Emit(std::string const& text) const -> void {
  _logger.Log(SDLRDP_LOG_INFO, text);
}
auto Diagnostics::Fail(std::string text) -> void {
  _errors.Publish(std::move(text));
}
}
