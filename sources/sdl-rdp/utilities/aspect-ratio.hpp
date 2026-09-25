#pragma once
#include <cstdint>

namespace sdl_rdp::utilities::detail::aspect_ratio {
struct AspectRatio {
  std::uint32_t numerator  { };
  std::uint32_t denominator{ };
};
constexpr auto operator==(AspectRatio left, AspectRatio right) noexcept -> bool {
  return left.numerator == right.numerator && left.denominator == right.denominator;
}
}

namespace sdl_rdp::utilities {
using detail::aspect_ratio::AspectRatio;
}
