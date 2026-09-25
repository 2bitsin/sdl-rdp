#pragma once
#include <type_traits>

namespace sdl_rdp::utilities::support_test::detail::out_of_range_enum {
// A C caller can pass any value in an ABI enum's storage; tests feed one the enum does not list.
template <class EnumTy>
  requires std::is_enum_v<EnumTy>
constexpr auto OutOfRangeEnum(std::underlying_type_t<EnumTy> value) -> EnumTy {
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): the value is out of range on purpose.
  return static_cast<EnumTy>(value);
}
}

namespace sdl_rdp::utilities::support_test {
using detail::out_of_range_enum::OutOfRangeEnum;
}
