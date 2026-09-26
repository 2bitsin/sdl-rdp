#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <functional>
#include <string_view>

namespace sdl_rdp::diagnostics::detail::failure_log {
using sdl_rdp::utilities::OperationName;

class FailureLog {
public:
       FailureLog(Diagnostics const& diagnostics, OperationName operation, LogLevel level = LogLevel::Error) noexcept;
  auto operator()(std::string_view failure) const -> void;

private:
  std::reference_wrapper<Diagnostics const> _diagnostics;
  OperationName                             _operation;
  LogLevel                                  _level;
};
auto FailuresOf(Diagnostics const& diagnostics, OperationName operation) noexcept -> FailureLog;
template <class SourceTy>
  requires requires(SourceTy const& source, OperationName operation) { source.Failures(operation); }
auto FailuresOf(SourceTy const& source, OperationName operation) noexcept -> FailureLog {
  return source.Failures(operation);
}
// A slot's failure projection: the diagnostics, or the failure source, the owner's accessor SOURCE returns.
template <auto SOURCE>
constexpr auto FailuresThrough = [](auto const& owner, OperationName operation) noexcept -> FailureLog {
  return FailuresOf(std::invoke(SOURCE, owner), operation);
};
}

namespace sdl_rdp::diagnostics {
using detail::failure_log::FailureLog;
using detail::failure_log::FailuresThrough;
}
