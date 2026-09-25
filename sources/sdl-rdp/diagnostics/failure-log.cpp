#include <sdl-rdp/diagnostics/failure-log.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>

#include <format>

namespace Backend {
FailureLog::FailureLog(Diagnostics const& diagnostics, OperationName operation, sdlrdp_log_level level) noexcept
    : _diagnostics{ diagnostics }, _operation{ operation }, _level{ level } { }
auto FailureLog::operator()(std::string_view failure) const -> void {
  _diagnostics.get().Log(_level, std::format("{} failed: {}.", _operation.View(), failure));
}
}
