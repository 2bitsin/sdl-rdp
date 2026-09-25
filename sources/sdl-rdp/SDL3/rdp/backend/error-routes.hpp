#pragma once
#include <sdl-rdp/utilities/contained.hpp>

#include <concepts>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
namespace sdl3::rdp::backend::detail::error_routes {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::FailureRoutes;

template <typename ActionTy>
concept BoundaryOperation = std::invocable<ActionTy>
                            && (std::same_as<std::invoke_result_t<ActionTy>, void>
                                || std::default_initializable<std::invoke_result_t<ActionTy>>);
// SDL_GetError() names the driver as the origin of an exception that carries no text of its own.
template <auto SET_ERROR, auto OUT_OF_MEMORY>
inline constexpr FailureRoutes ErrorRoutes{
  [](std::string_view text) { std::ignore = SET_ERROR("%s", std::string{ text }.c_str()); },
  [](std::bad_alloc const&) { std::ignore = OUT_OF_MEMORY(); }, "Unknown exception in RDP driver"
};
// SDL callbacks report failure through SDL_SetError and a default return value, never an exception.
template <auto const& ROUTES, BoundaryOperation ActionTy>
auto Bounded(ActionTy const& action) noexcept -> std::invoke_result_t<ActionTy> {
  if constexpr (std::same_as<std::invoke_result_t<ActionTy>, void>)
    std::ignore = Contained(action, ROUTES);
  else
    return Contained(std::invoke_result_t<ActionTy>{ }, action, ROUTES);
}
}

namespace sdl3::rdp::backend {
using detail::error_routes::BoundaryOperation;
using detail::error_routes::Bounded;
using detail::error_routes::ErrorRoutes;
}
