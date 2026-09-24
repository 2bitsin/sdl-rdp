#pragma once
#include <cstddef>
#include <string_view>

namespace Backend {
// Built only at compile time from an array, so the view it keeps points at static storage and cannot dangle.
class OperationName {
public:
  template <std::size_t N>
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays): a literal binds to an array reference
  consteval OperationName(char const (&text)[N]) : _text{ text, N - 1 } { }
  auto View() const noexcept -> std::string_view;

private:
  std::string_view _text;
};
}
