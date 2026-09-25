#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <functional>
#include <mutex>
#include <optional>

namespace sdl_rdp::headless_client_test::utilities::detail::published {
using sdl_rdp::utilities::Required;

// A channel context one thread's callback learns and another thread uses.
template <class ValueTy> class Published {
public:
  auto Publish(ValueTy& value) -> void {
    std::scoped_lock const lock(_guard);
    _value = value;
  }
  auto Withdraw() -> void {
    std::scoped_lock const lock(_guard);
    _value.reset();
  }
  [[nodiscard]] auto Peek() const -> std::optional<std::reference_wrapper<ValueTy>> {
    std::scoped_lock const lock(_guard);
    return _value;
  }
  // The reference outlives the lock: the publisher withdraws only after every reader is done with the channel.
  [[nodiscard]] auto Get() const -> ValueTy& {
    std::scoped_lock const lock(_guard);
    return Required(_value, "the channel context is published").get();
  }

private:
  mutable std::mutex                             _guard;
  std::optional<std::reference_wrapper<ValueTy>> _value;
};
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::published::Published;
}
