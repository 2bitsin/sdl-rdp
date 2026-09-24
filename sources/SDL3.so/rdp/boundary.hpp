#pragma once
#include "SDL_rdpboundary.hpp"
#include <concepts>
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
namespace rdp {
template<typename _Action>
concept BoundaryOperation = std::invocable<_Action> &&
    (std::same_as<std::invoke_result_t<_Action>, void> || std::default_initializable<std::invoke_result_t<_Action>>);
// SDL callbacks report failure through SDL_SetError and a default return value, never an exception.
template<BoundaryOperation _Action>
auto Boundary(_Action const& action) noexcept -> std::invoke_result_t<_Action> {
  try{ return std::invoke(action); }
  catch (std::bad_alloc const&) { SDL_OutOfMemory(); }
  catch (std::exception const& error) { SDL_SetError("%s", error.what()); }
  catch (...) { SDL_SetError("Unknown exception in RDP driver"); }
  if constexpr (!std::same_as<std::invoke_result_t<_Action>, void>) return { };
}
}
