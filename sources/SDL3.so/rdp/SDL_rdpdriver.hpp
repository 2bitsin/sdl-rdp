#pragma once
#include "SDL_rdpbackend.hpp"
#include "SDL_rdpconfiguration.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <span>
namespace rdp {
class Driver {
public:
  Driver();
  template<Operation _Operation, typename... _Args>
    requires BackendOperation<_Operation, sdlrdp_handle*, _Args...>
  auto Call(_Args&&... args) const {
    return _backend.Call<_Operation>(_session.Get().second, std::forward<_Args>(args)...);
  }
  template<typename _Accept> requires std::invocable<_Accept const&, sdlrdp_event const&>
  void Poll(_Accept const& accept) const {
    std::array<sdlrdp_event, _EventBatch> events{ };
    for (auto count = _Poll(events); count; count = _Poll(events))
      std::ranges::for_each(std::span(events).first(count), std::cref(accept));
  }
  auto Options() const -> Settings const&;
  auto Config() const  -> sdlrdp_config const&;
  void AuthDisplay(SDL_PropertiesID properties) noexcept;
  template<typename _Failure = bool>
  auto Fail(_Failure failure = { }) const -> _Failure {
    _ReportError();
    return failure;
  }
  [[noreturn]] void Throw() const;
private:
  // Backend authentication callbacks carry an opaque context and borrowed C strings.
  template<Operation _Operation, AuthenticationCredential _Credential>
  static auto _Authenticate(void* context, char const* domain, char const* user, _Credential credential) -> int;
  auto _Poll(std::span<sdlrdp_event> events) const -> std::size_t;
  void _ReportError() const;
  static constexpr std::size_t  _EventBatch      = 64;
  Settings const                _settings;
  Configuration const           _config;
  Backend const                 _backend;
  std::atomic<SDL_PropertiesID> _auth_properties;
  Session const                 _session;
};
// The backend's authentication callback reports to this display's properties until it is withdrawn.
using DisplayAuthentication = std::pair<std::reference_wrapper<Driver>, SDL_PropertiesID>;
auto PublishAuthentication(Driver& driver, SDL_PropertiesID properties) -> DisplayAuthentication;
void WithdrawAuthentication(DisplayAuthentication const& authentication) noexcept;
using AuthenticationDisplay = utilities::RAIIWrap<DisplayAuthentication, PublishAuthentication, WithdrawAuthentication>;
}
