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
  void Add(Value value) {
    _total   += value;
    _maximum =  std::max(_maximum, value);
    ++_count;
  }
  Value    Total() const   noexcept { return _total; }
  Value    Maximum() const noexcept { return _maximum; }
  uint64_t Count() const   noexcept { return _count; }

private:
  Value    _total  { };
  Value    _maximum{ };
  uint64_t _count  { };
};
}
