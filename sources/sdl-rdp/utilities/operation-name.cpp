#include <sdl-rdp/utilities/operation-name.hpp>

namespace Backend {
auto OperationName::View() const noexcept -> std::string_view {
  return _text;
}
}
