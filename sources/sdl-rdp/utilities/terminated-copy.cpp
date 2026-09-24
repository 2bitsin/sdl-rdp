#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <ranges>

namespace Backend {
using utilities::Expects;
auto CopyTerminated(std::span<char> field, std::string_view text) -> void {
  Expects(!field.empty(), "a C text field has room for its terminator");
  auto const end = std::ranges::copy(text | std::views::take(field.size() - 1), field.begin()).out;
  std::ranges::fill(end, field.end(), '\0');
}
}
