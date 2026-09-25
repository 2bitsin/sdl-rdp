#pragma once
#include <algorithm>
#include <concepts>
#include <cstdint>

namespace sdl_rdp::utilities::detail::running_statistics {
template <typename Value>
concept Accumulable = std::totally_ordered<Value> && std::default_initializable<Value>
                      && requires(Value sum, Value item) {
                           { sum += item } -> std::same_as<Value&>;
                         };
template <Accumulable Value> class RunningStatistics {
public:
  auto Add(Value value) -> void {
    _total   += value;
    _maximum =  std::max(_maximum, value);
    ++_count;
  }
  auto Total() const noexcept -> Value {
    return _total;
  }
  auto Maximum() const noexcept -> Value {
    return _maximum;
  }
  auto Count() const noexcept -> std::uint64_t {
    return _count;
  }

private:
  Value         _total  { };
  Value         _maximum{ };
  std::uint64_t _count  { };
};
}

namespace sdl_rdp::utilities {
using detail::running_statistics::RunningStatistics;
}
