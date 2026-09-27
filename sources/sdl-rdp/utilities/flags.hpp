#pragma once
#include <concepts>
#include <type_traits>
#include <utility>

namespace sdl_rdp::utilities::detail::flags {
// An enum opts in by declaring `auto FlagSet(EnumTy) -> std::true_type;` beside it and importing the operations.
template <typename EnumTy>
concept FlagEnum = std::is_scoped_enum_v<EnumTy> && std::unsigned_integral<std::underlying_type_t<EnumTy>>
                   && requires(EnumTy set) {
                        { FlagSet(set) } -> std::same_as<std::true_type>;
                      };
template <FlagEnum EnumTy>
constexpr auto operator|(EnumTy left, EnumTy right) noexcept -> EnumTy {
  return static_cast<EnumTy>(std::to_underlying(left) | std::to_underlying(right));
}
template <FlagEnum EnumTy>
constexpr auto operator&(EnumTy left, EnumTy right) noexcept -> EnumTy {
  return static_cast<EnumTy>(std::to_underlying(left) & std::to_underlying(right));
}
template <FlagEnum EnumTy>
constexpr auto Has(EnumTy set, EnumTy flag) noexcept -> bool {
  return (set & flag) != EnumTy{ };
}
}

namespace sdl_rdp::utilities {
using detail::flags::FlagEnum;
using detail::flags::Has;
using detail::flags::operator&;
using detail::flags::operator|;
}
