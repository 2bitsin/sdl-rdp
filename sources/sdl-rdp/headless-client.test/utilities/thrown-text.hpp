#pragma once
#include <gtest/gtest.h>
#include <concepts>
#include <exception>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>

namespace sdl_rdp::headless_client_test::utilities::detail::thrown_text {
// The text of the one exception type a body must throw; anything else is the test's failure and empty text.
template <std::derived_from<std::exception> ExceptionTy, std::invocable BodyTy>
auto ThrownText(BodyTy const& body) -> std::string {
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<BodyTy>>)
      std::invoke(body);
    else
      std::ignore = std::invoke(body);
  } catch (ExceptionTy const& failure) {
    return failure.what();
  }
  ADD_FAILURE() << "the body did not throw " << typeid(ExceptionTy).name();
  return { };
}
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::thrown_text::ThrownText;
}
