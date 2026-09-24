#pragma once
#include <functional>
namespace rdp {
// SDL handles are C pointers; this policy and CheckedAcquisition are the only place their null value is spelled.
template <typename HandleTy, auto PROJECTION = std::identity{ }>
class PointerState {
public:
  static auto IsNull(HandleTy const& value) noexcept -> bool {
    return std::invoke(PROJECTION, value) == nullptr;
  }
  static auto MakeNull(HandleTy& value) noexcept -> void {
    std::invoke(PROJECTION, value) = nullptr;
  }
};
}
