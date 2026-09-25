#pragma once
#include "internals.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <concepts>
#include <functional>
#include <utility>
namespace sdl3::rdp::sdl::detail::checkedacquisition {
template <auto ACQUIRE>
class CheckedAcquisition {
public:
  template <typename... ArgsTy>
    requires std::invocable<decltype(ACQUIRE), ArgsTy...>
  auto operator()(ArgsTy&&... args) const -> decltype(auto) {
    auto value = std::invoke(ACQUIRE, std::forward<ArgsTy>(args)...);
    if (!value) throw RelayedFailure{ SDL_GetError() };
    return value;
  }
};
}

namespace sdl3::rdp::sdl {
using detail::checkedacquisition::CheckedAcquisition;
}
