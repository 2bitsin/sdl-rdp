#pragma once
#include <charconv>
#include <concepts>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace Backend {
// oxbox #49: number_text::WholeNumber widened to floating point replaces this.
template <std::floating_point RealTy>
auto ParseReal(std::string_view text) -> std::optional<RealTy> {
  RealTy            value  { };
  auto const* const first  = std::to_address(text.begin());
  auto const* const last   = std::to_address(text.end());
  auto const        parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{ } || parsed.ptr != last) return std::nullopt;
  return value;
}
}
