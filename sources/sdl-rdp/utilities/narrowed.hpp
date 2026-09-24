#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <concepts>
#include <utility>

namespace Backend {
template <std::integral ToTy, std::integral FromTy> constexpr auto Narrowed(FromTy value) -> ToTy {
  utilities::Expects(std::in_range<ToTy>(value), "the value fits the narrower type");
  return static_cast<ToTy>(value);
}
}
