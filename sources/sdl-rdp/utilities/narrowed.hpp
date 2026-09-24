#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <concepts>
#include <utility>

namespace Backend {
template <std::integral _To, std::integral _From> constexpr auto Narrowed(_From value) -> _To {
  utilities::Expects(std::in_range<_To>(value), "the value fits the narrower type");
  return static_cast<_To>(value);
}
}
