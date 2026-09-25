#pragma once
#include <concepts>
#include <exception>
#include <functional>
#include <string_view>
#include <type_traits>

namespace Backend::detail::contained {
// A sink that throws while reporting has no further route, so its failure leaves only the status.
template <std::invocable<std::string_view> SinkTy>
auto Report(SinkTy const& on_failure, std::string_view text) noexcept -> void {
  try {
    std::invoke(on_failure, text);
  } catch (...) {
    return;
  }
}
}
namespace Backend {
inline constexpr std::string_view UnknownException = "unknown exception";
// A C caller cannot unwind: an exception from the body becomes the failure status and its text goes to the sink.
template <std::invocable BodyTy, std::invocable<std::string_view> SinkTy>
auto Contained(std::invoke_result_t<BodyTy> failure, BodyTy const& body, SinkTy const& on_failure) noexcept
    -> std::invoke_result_t<BodyTy> {
  try {
    return std::invoke(body);
  } catch (std::exception const& error) {
    detail::contained::Report(on_failure, error.what());
  } catch (...) {
    detail::contained::Report(on_failure, UnknownException);
  }
  return failure;
}
// Reports the failure where it is raised and hands it back for the caller to throw.
template <std::derived_from<std::exception> ExceptionTy, std::invocable<std::string_view> ReportTy>
auto Reported(ExceptionTy failure, ReportTy const& report) noexcept -> ExceptionTy {
  detail::contained::Report(report, failure.what());
  return failure;
}
}
