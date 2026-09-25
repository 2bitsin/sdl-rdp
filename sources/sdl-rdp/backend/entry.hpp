#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/diagnostics/error-store.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::backend::detail::entry {
template <class BodyTy> using Result = std::invoke_result_t<BodyTy, sdlrdp_handle&>;
template <class ResultTy> auto Refused(ResultTy failure, std::string_view subject) noexcept -> ResultTy {
  auto const publish = [&] {
    Backend::ErrorStore::PublishDetached(Backend::NullArgument{ subject }.what());
    return failure;
  };
  return Backend::Contained(failure, publish, [](std::string_view) noexcept { });
}
template <std::invocable<sdlrdp_handle&> BodyTy>
auto Serviced(sdlrdp_handle& handle, Result<BodyTy> failure, BodyTy const& body) noexcept -> Result<BodyTy> {
  auto const failing = [&handle](std::string_view text) { Backend::SetError(handle, std::string{ text }); };
  return Backend::Contained(failure, [&] { return body(handle); }, failing);
}
// An sdlrdp_* caller may pass a null handle: refused here once, the body receives the open handle.
template <std::invocable<sdlrdp_handle&> BodyTy>
auto Guarded(sdlrdp_handle* handle, Result<BodyTy> failure, std::string_view subject, BodyTy const& body) noexcept
    -> Result<BodyTy> {
  if (!handle) return Refused(failure, subject);
  return Serviced(*handle, failure, body);
}
}
namespace sdl_rdp::backend {
using detail::entry::Guarded;
using detail::entry::Refused;
using detail::entry::Serviced;
}
