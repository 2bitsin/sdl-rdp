#include <sdl-rdp/diagnostics/diagnostics.hpp>

namespace sdl_rdp::diagnostics::detail::diagnostics {
Diagnostics::Diagnostics(LogSink& sink, bool tracing) : _route{ sink }, _tracing{ tracing } { }
auto Diagnostics::Log(LogLevel level, std::string_view text) const -> void {
  _route.Log(level, text);
}
auto Diagnostics::Tracing() const noexcept -> bool {
  return _tracing;
}
auto Diagnostics::Emit(std::string_view text) const -> void {
  _route.Log(LogLevel::Info, text);
}
}
