#pragma once
#include "sdl-internals.hpp"
#include <concepts>
#include <functional>
#include <stdexcept>
#include <utility>
namespace sdl3::rdp::backend::detail::checkedacquisition {
template <auto ACQUIRE>
class CheckedAcquisition {
public:
  template <typename... ArgsTy>
    requires std::invocable<decltype(ACQUIRE), ArgsTy...>
  auto operator()(ArgsTy&&... args) const -> decltype(auto) {
    auto value = std::invoke(ACQUIRE, std::forward<ArgsTy>(args)...);
    if (!value) throw std::runtime_error(SDL_GetError());
    return value;
  }
};
}
namespace sdl3::rdp::backend {
using detail::checkedacquisition::CheckedAcquisition;
}
