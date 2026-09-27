#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <functional>
#include <string_view>

namespace sdl_rdp::diagnostics::detail::logged_failures {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::utilities::OperationName;

// A slot owner's failure sink: what INTERFACE's slots report is logged through the diagnostics it is given.
template <class InterfaceTy> class LoggedFailures : public InterfaceTy {
protected:
  explicit LoggedFailures(Diagnostics const& diagnostics) noexcept : _diagnostics{ diagnostics } { }
  auto     Logger() const noexcept -> Diagnostics const& {
    return _diagnostics;
  }

private:
  auto Failed(OperationName operation, std::string_view failure) const -> void final {
    FailureLog{ _diagnostics.get(), operation }(failure);
  }
  std::reference_wrapper<Diagnostics const> _diagnostics;
};
}

namespace sdl_rdp::diagnostics {
using detail::logged_failures::LoggedFailures;
}
