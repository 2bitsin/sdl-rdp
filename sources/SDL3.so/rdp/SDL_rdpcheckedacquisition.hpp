#pragma once
#include "SDL_rdpboundary.hpp"
#include <concepts>
#include <functional>
#include <stdexcept>
#include <utility>
namespace rdp {
template <auto _Acquire>
class CheckedAcquisition {
public:
  template <typename... _Args>
    requires std::invocable<decltype(_Acquire), _Args...>
  auto operator()(_Args&&... args) const -> decltype(auto) {
    auto value = std::invoke(_Acquire, std::forward<_Args>(args)...);
    if (!value) throw std::runtime_error(SDL_GetError());
    return value;
  }
};
}
