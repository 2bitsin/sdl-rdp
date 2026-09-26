#pragma once
#include <cstddef>
#include <string_view>

namespace sdl_rdp::utilities::detail::operation_name {
// Built only at compile time from an array, so the view it keeps points at static storage and cannot dangle.
class OperationName {
public:
  template <std::size_t N>
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays): a literal binds to an array reference
  consteval      OperationName(char const (&text)[N]) : _text{ text, N - 1 } { }
  constexpr auto View() const noexcept -> std::string_view {
    return _text;
  }

private:
  std::string_view _text;
};
}

namespace sdl_rdp::utilities {
using detail::operation_name::OperationName;
}
