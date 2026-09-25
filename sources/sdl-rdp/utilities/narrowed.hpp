#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace sdl_rdp::utilities::detail::narrowed {
// std::in_range takes the standard integer types only; a character type is ranged as the integer of its width.
template <std::integral Ty>
using Ranged = std::conditional_t<std::is_signed_v<Ty>, std::make_signed_t<Ty>, std::make_unsigned_t<Ty>>;

template <std::integral ToTy, std::integral FromTy> constexpr auto Narrowed(FromTy value) -> ToTy {
  Expects(std::in_range<Ranged<ToTy>>(value), "the value fits the narrower type");
  return static_cast<ToTy>(value);
}
}

namespace sdl_rdp::utilities {
using detail::narrowed::Narrowed;
}
