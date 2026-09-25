#pragma once
#include "sdl-internals.hpp"
#include <concepts>
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
namespace sdl3::rdp::backend::detail::boundary {
template <typename ActionTy>
concept BoundaryOperation = std::invocable<ActionTy>
                            && (std::same_as<std::invoke_result_t<ActionTy>, void>
                                || std::default_initializable<std::invoke_result_t<ActionTy>>);
// SDL callbacks report failure through SDL_SetError and a default return value, never an exception.
template <BoundaryOperation ActionTy>
auto Boundary(ActionTy const& action) noexcept -> std::invoke_result_t<ActionTy> {
  try {
    return std::invoke(action);
  } catch (std::bad_alloc const&) {
    SDL_OutOfMemory();
  } catch (std::exception const& error) {
    SDL_SetError("%s", error.what());
  } catch (...) {
    SDL_SetError("Unknown exception in RDP driver");
  }
  if constexpr (!std::same_as<std::invoke_result_t<ActionTy>, void>) return { };
}
}
namespace sdl3::rdp::backend {
using detail::boundary::BoundaryOperation;
using detail::boundary::Boundary;
}
