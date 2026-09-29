#pragma once
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace sdl_rdp::utilities::detail::parameter {
template <class> struct Signature;
template <class ResultTy, bool NOEXCEPT, class... ArgsTy> struct Signature<ResultTy(ArgsTy...) noexcept(NOEXCEPT)> {
  using Parameters = std::tuple<ArgsTy...>;
};
template <class ResultTy, class ObjectTy, bool NOEXCEPT, class... ArgsTy>
struct Signature<ResultTy (ObjectTy::*)(ArgsTy...) noexcept(NOEXCEPT)> {
  using Parameters = std::tuple<ArgsTy...>;
};
template <class ResultTy, class ObjectTy, bool NOEXCEPT, class... ArgsTy>
struct Signature<ResultTy (ObjectTy::*)(ArgsTy...) const noexcept(NOEXCEPT)> {
  using Parameters = std::tuple<ArgsTy...>;
};

// A member function's parameters follow its object; a closure's parameters are its call operator's.
template <class CallableTy> struct Callable : Signature<std::remove_pointer_t<CallableTy>> { };
template <class CallableTy>
  requires std::is_class_v<CallableTy>
struct Callable<CallableTy> : Signature<decltype(&CallableTy::operator())> { };
template <auto CALLABLE> using Parameters = typename Callable<decltype(CALLABLE)>::Parameters;
template <auto CALLABLE, std::size_t INDEX> using Parameter = std::tuple_element_t<INDEX, Parameters<CALLABLE>>;
}

namespace sdl_rdp::utilities {
using detail::parameter::Parameter;
using detail::parameter::Parameters;
}
