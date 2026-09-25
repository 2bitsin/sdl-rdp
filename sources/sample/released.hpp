#pragma once

namespace sample::detail::released {
// A unique_ptr deleter over a C release function; unique_ptr calls it only with the handle it owns.
template <auto RELEASE>
class Released {
public:
  template <typename HandleTy> auto operator()(HandleTy* handle) const noexcept -> void {
    static_cast<void>(RELEASE(handle));
  }
};
}

namespace sample {
using detail::released::Released;
}
