#pragma once
#include <concepts>
#include <cstdint>
#include <utility>

namespace Backend {
template <std::movable _Value> class Generational {
public:
  auto Replace(_Value value) -> uint64_t {
    _value = std::move(value);
    return ++_generation;
  }
  auto Generation() const noexcept -> uint64_t {
    return _generation;
  }
  auto Value() const noexcept -> _Value const& {
    return _value;
  }

private:
  _Value   _value     { };
  uint64_t _generation{ };
};
}
