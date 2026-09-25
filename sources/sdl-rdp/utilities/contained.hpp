#pragma once
#include <sdl-rdp/utilities/operation-name.hpp>

#include <concepts>
#include <exception>
#include <functional>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sdl_rdp::utilities::detail::contained {
inline constexpr OperationName UnknownException{ "unknown exception" };
// A function or one call operator taking text: an overload set would promise typed failures Contained never delivers.
template <typename SinkTy>
concept TextSink = std::invocable<SinkTy const&, std::string_view>
                   && (std::is_function_v<SinkTy> || requires { &SinkTy::operator(); });
// The one typed route: exhaustion and a non-standard exception reach their own handlers, the rest is text.
// The unknown text is an OperationName, a literal's static storage, so a route never outlives its text.
template <TextSink TextTy, std::invocable<std::bad_alloc const&> ExhaustedTy> class FailureRoutes {
public:
  constexpr FailureRoutes(TextTy text, ExhaustedTy exhausted,
                          OperationName unknown) noexcept(std::is_nothrow_move_constructible_v<TextTy>
                                                          && std::is_nothrow_move_constructible_v<ExhaustedTy>)
      : _text{ std::move(text) }, _exhausted{ std::move(exhausted) }, _unknown{ unknown } { }
  auto      Text(std::string_view failure) const -> void {
    std::invoke(_text, failure);
  }
  auto Exhausted(std::bad_alloc const& failure) const -> void {
    std::invoke(_exhausted, failure);
  }
  auto Unknown() const -> void {
    std::invoke(_text, _unknown.View());
  }

private:
  TextTy        _text;
  ExhaustedTy   _exhausted;
  OperationName _unknown;
};
template <typename SinkTy> inline constexpr bool IsRoutes = false;
template <typename TextTy, typename ExhaustedTy>
inline constexpr bool IsRoutes<FailureRoutes<TextTy, ExhaustedTy>> = true;
template <typename SinkTy>
concept ContainedSink = TextSink<SinkTy> || IsRoutes<SinkTy>;
// A text sink's routes: every failure reaches it as text; the routes only refer to the sink.
template <TextSink SinkTy> auto TextRoutes(SinkTy const& sink) noexcept -> auto {
  return FailureRoutes{ [&sink](std::string_view text) { std::invoke(sink, text); },
                        [&sink](std::bad_alloc const& failure) { std::invoke(sink, failure.what()); },
                        UnknownException };
}
// A sink that throws while reporting has no further route, so its failure leaves only the status.
template <typename RoutesTy, typename RouteTy, typename... ArgsTy>
auto Report(RoutesTy const& routes, RouteTy route, ArgsTy const&... args) noexcept -> void {
  try {
    std::invoke(route, routes, args...);
  } catch (...) {
    return;
  }
}
template <std::invocable BodyTy, typename RoutesTy>
  requires IsRoutes<RoutesTy>
auto Routed(std::invoke_result_t<BodyTy> failure, BodyTy const& body, RoutesTy const& routes) noexcept
    -> std::invoke_result_t<BodyTy> {
  try {
    return std::invoke(body);
  } catch (std::bad_alloc const& error) {
    Report(routes, &RoutesTy::Exhausted, error);
  } catch (std::exception const& error) {
    Report(routes, &RoutesTy::Text, std::string_view{ error.what() });
  } catch (...) {
    Report(routes, &RoutesTy::Unknown);
  }
  return failure;
}
// Outside the noexcept caller: bugprone-exception-escape reads a lambda's body as its enclosing function's.
template <std::invocable BodyTy> auto Completing(BodyTy const& body) -> auto {
  return [&body] {
    std::invoke(body);
    return true;
  };
}
// A C caller cannot unwind: an exception from the body becomes the failure status and goes to the sink.
template <std::invocable BodyTy, ContainedSink SinkTy>
auto Contained(std::invoke_result_t<BodyTy> failure, BodyTy const& body, SinkTy const& on_failure) noexcept
    -> std::invoke_result_t<BodyTy> {
  if constexpr (IsRoutes<SinkTy>)
    return Routed(std::move(failure), body, on_failure);
  else
    return Routed(std::move(failure), body, TextRoutes(on_failure));
}
// A body without a result answers whether it completed.
template <std::invocable BodyTy, ContainedSink SinkTy>
  requires std::is_void_v<std::invoke_result_t<BodyTy>>
auto Contained(BodyTy const& body, SinkTy const& on_failure) noexcept -> bool {
  return Contained(false, Completing(body), on_failure);
}
// Reports the failure where it is raised and hands it back for the caller to throw.
template <std::derived_from<std::exception> ExceptionTy, TextSink ReportTy>
auto Reported(ExceptionTy failure, ReportTy const& report) noexcept -> ExceptionTy {
  auto const routes = TextRoutes(report);
  Report(routes, &std::remove_const_t<decltype(routes)>::Text, std::string_view{ failure.what() });
  return failure;
}
}

namespace sdl_rdp::utilities {
using detail::contained::Contained;
using detail::contained::ContainedSink;
using detail::contained::FailureRoutes;
using detail::contained::Reported;
using detail::contained::UnknownException;
}
