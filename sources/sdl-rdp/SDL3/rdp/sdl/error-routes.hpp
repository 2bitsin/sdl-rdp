#pragma once
#include <sdl-rdp/utilities/contained.hpp>

#include <concepts>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
namespace sdl3::rdp::sdl::detail::error_routes {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::FailureRoutes;

template <typename ActionTy>
concept VoidOperation = std::invocable<ActionTy> && std::same_as<std::invoke_result_t<ActionTy>, void>;
template <typename ActionTy>
concept ValueOperation = std::invocable<ActionTy> && std::default_initializable<std::invoke_result_t<ActionTy>>;
// SDL_GetError() names the driver as the origin of an exception that carries no text of its own.
template <auto SET_ERROR, auto OUT_OF_MEMORY>
inline constexpr FailureRoutes ErrorRoutes{
  [](std::string_view text) { std::ignore = SET_ERROR("%s", std::string{ text }.c_str()); },
  [](std::bad_alloc const&) { std::ignore = OUT_OF_MEMORY(); }, "Unknown exception in RDP driver"
};
// SDL callbacks report failure through SDL_SetError and a return value, never an exception: the stated failure
// value, or the result type's default.
template <auto const& ROUTES, VoidOperation ActionTy> auto Bounded(ActionTy const& action) noexcept -> void {
  std::ignore = Contained(action, ROUTES);
}
template <auto const& ROUTES, ValueOperation ActionTy>
auto Bounded(ActionTy const& action, std::type_identity_t<std::invoke_result_t<ActionTy>> failure = { }) noexcept
    -> std::invoke_result_t<ActionTy> {
  return Contained(std::move(failure), action, ROUTES);
}
}

namespace sdl3::rdp::sdl {
using detail::error_routes::Bounded;
using detail::error_routes::ErrorRoutes;
using detail::error_routes::ValueOperation;
using detail::error_routes::VoidOperation;
}
