#pragma once
#include "sdl-internals.hpp"
#include <sdl-rdp/SDL3/rdp/backend/error-routes.hpp>

#include <type_traits>
namespace sdl3::rdp::backend::detail::boundary {
template <BoundaryOperation ActionTy>
auto Boundary(ActionTy const& action) noexcept -> std::invoke_result_t<ActionTy> {
  return Bounded<ErrorRoutes<&SDL_SetError, &SDL_OutOfMemory>>(action);
}
}
namespace sdl3::rdp::backend {
using detail::boundary::Boundary;
}
