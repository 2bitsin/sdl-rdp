#pragma once
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/parameter.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::handled {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Parameters;

struct                  NoFailure { };
template <class> struct OwnerFirst;
template <class OwnerTy, class... ArgsTy> struct OwnerFirst<std::tuple<OwnerTy, ArgsTy...>> {
  using Owner = OwnerTy;
  using type  = std::tuple<ArgsTy...>;
};
// A member function takes the arguments after its object; any other handler takes the owner first.
template <auto HANDLER>
using HandlerParameters = typename std::conditional_t<std::is_member_function_pointer_v<decltype(HANDLER)>,
                                                      std::type_identity<Parameters<HANDLER>>,
                                                      OwnerFirst<Parameters<HANDLER>>>::type;
// A trailing slot argument the handler does not declare.
struct Unused{ };
template <auto HANDLER, std::size_t INDEX>
using SlotParameter = typename std::conditional_t<(INDEX < std::tuple_size_v<HandlerParameters<HANDLER>>),
                                                  std::tuple_element<INDEX, HandlerParameters<HANDLER>>,
                                                  std::type_identity<Unused>>::type;
// A registration's `void*` user data is the object the owner projection takes.
template <auto OWNER>
using UserData = std::remove_reference_t<typename OwnerFirst<Parameters<OWNER>>::Owner>;
template <class> constexpr bool FixedSpan = false;
template <class ElementTy, std::size_t EXTENT>
constexpr bool FixedSpan<std::span<ElementTy, EXTENT>> = EXTENT != std::dynamic_extent;

// A C slot passes each record by pointer: the handler receives it as a reference, checked here once, and a buffer the
// handler takes as a fixed-extent span as that span.
template <class ParameterTy, class ArgTy> auto Referenced(ArgTy argument) -> decltype(auto) {
  if constexpr (std::same_as<ParameterTy, Unused>) {
    return Unused{ };
  } else if constexpr (!std::is_pointer_v<ArgTy>) {
    return argument;
  } else if constexpr (FixedSpan<ParameterTy>) {
    Expects(argument != nullptr, "the callback buffer is supplied");
    return ParameterTy{ argument, ParameterTy::extent };
  } else {
    Expects(argument != nullptr, "the callback argument is supplied");
    return *argument;
  }
}

// The handler takes the slot's leading arguments, as many as it declares.
template <auto HANDLER, class OwnerTy, class... ReferencedTy>
auto Invoked(OwnerTy& owner, ReferencedTy&&... referenced) -> decltype(auto) {
  auto arguments = std::forward_as_tuple(std::forward<ReferencedTy>(referenced)...);
  return [&]<std::size_t... INDEX>(std::index_sequence<INDEX...>) -> decltype(auto) {
    return std::invoke(HANDLER, owner, std::get<INDEX>(arguments)...);
  }(std::make_index_sequence<std::tuple_size_v<HandlerParameters<HANDLER>>>{ });
}

// Each slot argument is converted to the parameter the handler declares at its position, then the handler is called.
template <auto HANDLER, class OwnerTy, class... ArgsTy> auto Dispatched(OwnerTy& owner, ArgsTy... args)
    -> decltype(auto) {
  return [&]<std::size_t... INDEX>(std::index_sequence<INDEX...>) -> decltype(auto) {
    return Invoked<HANDLER>(owner, Referenced<SlotParameter<HANDLER, INDEX>>(args)...);
  }(std::index_sequence_for<ArgsTy...>{ });
}

// The registration hands back the owner it was given.
template <class OwnerTy> constexpr auto Itself = [](OwnerTy& owner) -> OwnerTy& { return owner; };

// abi: the slot's signature is the C table's; its context is checked once and the owner found through it, its
// leading arguments reach the handler as references, and a throw becomes FAILURE, reported through FAILURES.
template <auto OWNER, auto HANDLER, auto const& OPERATION, auto FAILURES, auto FAILURE = NoFailure{ }, class ResultTy,
          class ContextTy, class... ArgsTy>
auto Handled(ContextTy* context, ArgsTy... args) noexcept -> ResultTy {
  static_assert(std::is_void_v<ResultTy> || !std::same_as<decltype(FAILURE), NoFailure>, "a result names its failure");
  Expects(context != nullptr, "callback context exists");
  auto&      owner    = [&] -> decltype(auto) {
    if constexpr (std::is_void_v<ContextTy>)
      return std::invoke(OWNER, *static_cast<UserData<OWNER>*>(context));
    else
      return std::invoke(OWNER, *context);
  }();
  auto const reported = [&](std::string_view failure) { std::invoke(FAILURES, owner, OPERATION)(failure); };
  auto const handled  = [&] -> decltype(auto) { return Dispatched<HANDLER>(owner, args...); };
  if constexpr (std::is_void_v<ResultTy>)
    std::ignore = Contained([&] { handled(); }, reported);
  else
    return Contained(ResultTy{ FAILURE }, [&] -> ResultTy { return handled(); }, reported);
}
}

namespace sdl_rdp::freerdp_facade {
using detail::handled::Handled;
using detail::handled::Itself;
}
