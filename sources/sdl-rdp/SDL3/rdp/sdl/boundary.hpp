#pragma once
#include "internals.hpp"
#include <sdl-rdp/SDL3/rdp/sdl/error-routes.hpp>

#include <type_traits>
#include <utility>
namespace sdl3::rdp::sdl::detail::boundary {
// A slot's body; its failure value follows when it is not the result type's default.
template <VoidOperation ActionTy> auto Boundary(ActionTy const& action) noexcept -> void {
  Bounded<ErrorRoutes<&SDL_SetError, &SDL_OutOfMemory>>(action);
}
template <ValueOperation ActionTy>
auto Boundary(ActionTy const& action, std::type_identity_t<std::invoke_result_t<ActionTy>> failure = { }) noexcept
    -> std::invoke_result_t<ActionTy> {
  return Bounded<ErrorRoutes<&SDL_SetError, &SDL_OutOfMemory>>(action, std::move(failure));
}
}

namespace sdl3::rdp::sdl {
using detail::boundary::Boundary;
}
