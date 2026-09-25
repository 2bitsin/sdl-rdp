#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <concepts>
#include <cstdint>
#include <utility>

namespace sdl_rdp::utilities::detail::bounded {
// An integer with a real range; its decode also refuses what oxbox's reader would narrow unchecked (2bitsin/oxbox#2).
template <std::integral ValueTy, ValueTy MINIMUM, ValueTy MAXIMUM>
class Bounded {
  static_assert(MINIMUM <= MAXIMUM, "a bounded range is ordered");
  static_assert(std::in_range<std::int64_t>(MINIMUM), "the minimum has a wire value");
  static_assert(std::in_range<std::int64_t>(MAXIMUM), "the maximum has a wire value");
public:
  using value_type = ValueTy;
  constexpr          Bounded() = default;
  constexpr explicit Bounded(ValueTy value) : _value{ value } {
    auto const admitted = Admits(value);
    Expects(admitted, "a bounded value is within its range");
  }
  static constexpr auto Minimum() -> std::int64_t {
    return MINIMUM;
  }
  static constexpr auto Maximum() -> std::int64_t {
    return MAXIMUM;
  }
  static constexpr auto Admits(std::int64_t value) -> bool {
    return std::cmp_greater_equal(value, MINIMUM) && std::cmp_less_equal(value, MAXIMUM);
  }
  static auto _Decode(std::int64_t wire) -> Bounded {
    if (!Admits(wire)) throw OutOfRange{ "Value", wire, Minimum(), Maximum() };
    return Bounded{ static_cast<ValueTy>(wire) };
  }
  constexpr auto _Encode() const -> std::int64_t {
    return _value;
  }
  constexpr auto Get() const -> ValueTy {
    return _value;
  }
  constexpr auto operator==(Bounded const&) const -> bool = default;
private:
  ValueTy _value{ MINIMUM };
};
template <typename ValueTy>
inline constexpr bool IsBounded = false;
template <std::integral ValueTy, ValueTy MINIMUM, ValueTy MAXIMUM>
inline constexpr bool IsBounded<Bounded<ValueTy, MINIMUM, MAXIMUM>> = true;
template <typename ValueTy>
concept BoundedInteger = IsBounded<ValueTy>;
}

namespace sdl_rdp::utilities {
using detail::bounded::Bounded;
using detail::bounded::BoundedInteger;
}
