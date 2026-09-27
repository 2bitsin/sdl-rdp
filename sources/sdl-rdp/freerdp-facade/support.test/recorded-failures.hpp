#pragma once
#include <sdl-rdp/utilities/operation-name.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::freerdp_facade::support_test::detail::recorded_failures {
using sdl_rdp::utilities::OperationName;

// A test's slot owner for each interface: the operations whose handlers threw, in order.
template <class... InterfacesTy> class RecordedFailures : public InterfacesTy... {
protected:
  explicit RecordedFailures(std::vector<std::string>& failures) noexcept : _failures{ failures } { }

private:
  auto Failed(OperationName operation, std::string_view /*failure*/) const -> void final {
    _failures.get().emplace_back(operation.View());
  }
  std::reference_wrapper<std::vector<std::string>> _failures;
};
}

namespace sdl_rdp::freerdp_facade::support_test {
using detail::recorded_failures::RecordedFailures;
}
