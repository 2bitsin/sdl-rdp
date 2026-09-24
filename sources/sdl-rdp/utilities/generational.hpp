#pragma once
#include <concepts>
#include <cstdint>
#include <utility>

namespace Backend {
template <std::movable ValueTy> class Generational {
public:
  auto Replace(ValueTy value) -> std::uint64_t {
    _value = std::move(value);
    return ++_generation;
  }
  auto Generation() const noexcept -> std::uint64_t {
    return _generation;
  }
  auto Value() const noexcept -> ValueTy const& {
    return _value;
  }

private:
  ValueTy       _value     { };
  std::uint64_t _generation{ };
};
}
