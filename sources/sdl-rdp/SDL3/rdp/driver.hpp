#pragma once
#include <sdl-rdp/SDL3/rdp/backend/backend.hpp>
#include <sdl-rdp/SDL3/rdp/settings/configuration.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <span>
namespace sdl3::rdp::detail::driver {
class Driver {
public:
  Driver();
  template <backend::Operation OPERATION, typename... ArgsTy>
    requires backend::BackendOperation<OPERATION, sdlrdp_handle*, ArgsTy...>
  auto Call(ArgsTy&&... args) const -> decltype(auto) {
    return _backend.Call<OPERATION>(_session.Get().second, std::forward<ArgsTy>(args)...);
  }
  template <typename AcceptTy>
    requires std::invocable<AcceptTy const&, sdlrdp_event const&>
  auto Poll(AcceptTy const& accept) const -> void {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): ES.20 input buffer, the poll writes what it reports
    std::array<sdlrdp_event, _EventBatch> events;
    for (auto count = _Poll(events); count; count = _Poll(events))
      std::ranges::for_each(std::span(events).first(count), std::cref(accept));
  }
  auto Options() const                                   -> settings::Options const&;
  auto Config() const                                    -> sdlrdp_config const&;
  auto AuthDisplay(SDL_PropertiesID properties) noexcept -> void;
  template <typename FailureTy = bool>
  auto Fail(FailureTy failure = { }) const -> FailureTy {
    _ReportError();
    return failure;
  }
  [[noreturn]] auto Throw() const -> void;
private:
  // Backend authentication callbacks carry an opaque context and borrowed C strings.
  template <backend::Operation OPERATION, settings::AuthenticationCredential CredentialTy>
  static auto _Authenticate(void* context, char const* domain, char const* user, CredentialTy credential) -> int;
  auto _Poll(std::span<sdlrdp_event> events) const -> std::size_t;
  auto _ReportError() const                        -> void;
  static constexpr std::size_t  _EventBatch      = 64;
  settings::Options const       _options;
  settings::Configuration const _config;
  backend::Backend const        _backend;
  std::atomic<SDL_PropertiesID> _auth_properties;
  backend::Session const        _session;
};
// The backend's authentication callback reports to this display's properties until it is withdrawn.
using DisplayAuthentication = std::pair<std::reference_wrapper<Driver>, SDL_PropertiesID>;
auto PublishAuthentication(Driver& driver, SDL_PropertiesID properties)           -> DisplayAuthentication;
auto WithdrawAuthentication(DisplayAuthentication const& authentication) noexcept -> void;
using AuthenticationDisplay = utilities::RAIIWrap<DisplayAuthentication, PublishAuthentication, WithdrawAuthentication>;
}
namespace sdl3::rdp {
using detail::driver::Driver;
using detail::driver::DisplayAuthentication;
using detail::driver::PublishAuthentication;
using detail::driver::WithdrawAuthentication;
using detail::driver::AuthenticationDisplay;
}
