#pragma once
#include <functional>
namespace rdp {
// SDL handles are C pointers; this policy and CheckedAcquisition are the only place their null value is spelled.
template<typename _Handle, auto _Projection = std::identity{ }>
class PointerState {
public:
  static auto IsNull(_Handle const& value) noexcept -> bool { return std::invoke(_Projection, value) == nullptr; }
  static auto MakeNull(_Handle& value) noexcept -> void { std::invoke(_Projection, value) = nullptr; }
};
}
