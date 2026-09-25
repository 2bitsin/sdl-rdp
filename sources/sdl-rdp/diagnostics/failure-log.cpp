#include <sdl-rdp/diagnostics/failure-log.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>

#include <format>

namespace sdl_rdp::diagnostics::detail::failure_log {
FailureLog::FailureLog(Diagnostics const& diagnostics, OperationName operation, sdlrdp_log_level level) noexcept
    : _diagnostics{ diagnostics }, _operation{ operation }, _level{ level } { }
auto FailureLog::operator()(std::string_view failure) const -> void {
  _diagnostics.get().Log(_level, std::format("{} failed: {}.", _operation.View(), failure));
}
auto FailuresOf(Diagnostics const& diagnostics, OperationName operation) noexcept -> FailureLog {
  return { diagnostics, operation };
}
}
