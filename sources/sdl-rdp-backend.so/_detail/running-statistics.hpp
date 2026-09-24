#pragma once
#include <algorithm>
#include <concepts>
#include <cstdint>

namespace Backend {
template <typename Value>
concept Accumulable = std::totally_ordered<Value> && std::default_initializable<Value> &&
                      requires(Value sum, Value item) {
  { sum += item } -> std::same_as<Value&>;
};
template <Accumulable Value> class RunningStatistics {
public:
  auto Add(Value value) -> void {
    _total   += value;
    _maximum =  std::max(_maximum, value);
    ++_count;
  }
  auto Total() const noexcept   -> Value { return _total; }
  auto Maximum() const noexcept -> Value { return _maximum; }
  auto Count() const noexcept   -> uint64_t { return _count; }

private:
  Value    _total  { };
  Value    _maximum{ };
  uint64_t _count  { };
};
}
