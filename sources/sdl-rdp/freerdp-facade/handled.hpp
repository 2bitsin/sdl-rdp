#pragma once
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::handled {
struct                  NoFailure{ };
template <class> struct Signature;
template <class ResultTy, class OwnerTy,
          class... ArgsTy> struct Signature<std::function<ResultTy(OwnerTy, ArgsTy...)>> {
  using Owner      = OwnerTy;
  using Parameters = std::tuple<ArgsTy...>;
};
template <class ResultTy, class OwnerTy, bool NOEXCEPT, class... ArgsTy>
struct Signature<ResultTy (OwnerTy::*)(ArgsTy...) noexcept(NOEXCEPT)> {
  using Parameters = std::tuple<ArgsTy...>;
};
template <class ResultTy, class OwnerTy, bool NOEXCEPT, class... ArgsTy>
struct Signature<ResultTy (OwnerTy::*)(ArgsTy...) const noexcept(NOEXCEPT)> {
  using Parameters = std::tuple<ArgsTy...>;
};
// A member function takes the arguments after its object; any other handler takes the owner first.
template <auto HANDLER> consteval auto SignatureOf() {
  if constexpr (std::is_member_function_pointer_v<decltype(HANDLER)>)
    return Signature<decltype(HANDLER)>{ };
  else
    return Signature<decltype(std::function{ HANDLER })>{ };
}
template <auto HANDLER> using HandlerParameters = typename decltype(SignatureOf<HANDLER>())::Parameters;
// A registration's `void*` user data is the object the owner projection takes.
template <auto OWNER>
using UserData = std::remove_reference_t<typename Signature<decltype(std::function{ OWNER })>::Owner>;
template <class> constexpr bool FixedSpan = false;
template <class ElementTy, std::size_t EXTENT>
constexpr bool FixedSpan<std::span<ElementTy, EXTENT>> = EXTENT != std::dynamic_extent;

// A C slot passes each record by pointer: the handler receives it as a reference, checked here once, and a buffer the
// handler takes as a fixed-extent span as that span.
template <class ParameterTy, class ArgTy> auto Referenced(ArgTy argument) -> decltype(auto) {
  if constexpr (!std::is_pointer_v<ArgTy>) {
    return argument;
  } else if constexpr (FixedSpan<ParameterTy>) {
    ::utilities::Expects(argument != nullptr, "the callback buffer is supplied");
    return ParameterTy{ argument, ParameterTy::extent };
  } else {
    ::utilities::Expects(argument != nullptr, "the callback argument is supplied");
    return *argument;
  }
}

template <auto HANDLER, class OwnerTy, class... ArgsTy>
auto Invoked(OwnerTy& owner, ArgsTy... args) -> decltype(auto) {
  using ParametersTy = HandlerParameters<HANDLER>;
  auto const arguments = std::tuple{ args... };
  return [&]<std::size_t... INDEX>(std::index_sequence<INDEX...>) -> decltype(auto) {
    return std::invoke(HANDLER, owner,
                       Referenced<std::tuple_element_t<INDEX, ParametersTy>>(std::get<INDEX>(arguments))...);
  }(std::make_index_sequence<std::tuple_size_v<ParametersTy>>{ });
}

// The registration hands back the owner it was given.
template <class OwnerTy> constexpr auto Itself   = [](OwnerTy& owner) -> OwnerTy& { return owner; };
template <class> struct                 MemberOf;
template <class MemberTy, class OwnerTy> struct MemberOf<MemberTy OwnerTy::*> {
  using Owner = OwnerTy;
};
// A channel-id handler: the owner's slot, the member SLOT names, takes the assigned id.
template <auto SLOT>
constexpr auto AssignThrough = [](typename MemberOf<decltype(SLOT)>::Owner& owner, std::uint32_t id) -> bool {
  return std::invoke(SLOT, owner).Assign(id);
};

// abi: the slot's signature is the C table's; its context is checked once and the owner found through it, its
// leading arguments reach the handler as references, and a throw becomes FAILURE, reported through FAILURES.
template <auto OWNER, auto HANDLER, auto const& OPERATION, auto FAILURES, auto FAILURE = NoFailure{ }, class ResultTy,
          class ContextTy, class... ArgsTy>
auto Handled(ContextTy* context, ArgsTy... args) noexcept -> ResultTy {
  static_assert(std::is_void_v<ResultTy> || !std::same_as<decltype(FAILURE), NoFailure>, "a result names its failure");
  ::utilities::Expects(context != nullptr, "callback context exists");
  auto&      owner    = [&] -> decltype(auto) {
    if constexpr (std::is_void_v<ContextTy>)
      return std::invoke(OWNER, *static_cast<UserData<OWNER>*>(context));
    else
      return std::invoke(OWNER, *context);
  }();
  auto const reported = [&](std::string_view failure) { std::invoke(FAILURES, owner, OPERATION)(failure); };
  auto const handled  = [&] -> decltype(auto) { return Invoked<HANDLER>(owner, args...); };
  if constexpr (std::is_void_v<ResultTy>)
    std::ignore = Backend::Contained(
        false,
        [&] {
          handled();
          return true;
        },
        reported);
  else
    return Backend::Contained(ResultTy(FAILURE), [&] -> ResultTy { return handled(); }, reported);
}
}

namespace sdl_rdp::freerdp_facade {
using detail::handled::AssignThrough;
using detail::handled::Handled;
using detail::handled::Itself;
}
