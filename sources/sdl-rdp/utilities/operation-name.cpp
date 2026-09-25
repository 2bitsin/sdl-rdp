#include <sdl-rdp/utilities/operation-name.hpp>

namespace sdl_rdp::utilities::detail::operation_name {
auto OperationName::View() const noexcept -> std::string_view {
  return _text;
}
}
