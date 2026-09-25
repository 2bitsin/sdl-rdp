#pragma once
#include "check.hpp"
#include <string_view>
#include <utility>

namespace sample::detail::printed {
// printf's `%.*s` takes a view's length as an int precision.
inline auto Printed(std::string_view text) -> int {
  Check(std::in_range<int>(text.size()));
  return static_cast<int>(text.size());
}
}

namespace sample {
using detail::printed::Printed;
}
