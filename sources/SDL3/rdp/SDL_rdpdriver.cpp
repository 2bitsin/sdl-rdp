#include "SDL_rdpdriver.hpp"
#include <stdexcept>
namespace rdp {
namespace {
auto BackendPath(Settings const& settings) -> std::filesystem::path {
  auto const path = settings.Get(SDL_HINT_RDP_BACKEND).value_or("");
  return path.empty() ? SDL_RDP_DYNAMIC : path;
}
}
template <Operation _Operation, AuthenticationCredential _Credential>
auto Driver::_Authenticate(void* context, char const* domain, char const* user, _Credential credential) -> int {
  utilities::Expects(context != nullptr, "authentication has its driver context");
  auto const&    self       = *static_cast<Driver const*>(context);
  constexpr auto name       = _Operation == Operation::VERIFY_PAIR ? SDL_PROP_DISPLAY_RDP_VERIFY_POINTER
                                                                   : SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER;
  auto const     properties = self._auth_properties.load();
  // SDL display properties hold the application's authentication callback as an opaque pointer.
  auto const     callback   = reinterpret_cast<AuthenticationCallback<_Credential>>(
      properties ? SDL_GetPointerProperty(properties, name, nullptr) : nullptr);
  if (!callback) return self._backend.Call<_Operation>(&self._config.Get(), domain, user, credential);
  return callback(SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, nullptr), domain, user,
                  credential);
}
Driver::Driver()
    : _config{ _settings, _Authenticate<Operation::VERIFY_PAIR, char const*>,
               _Authenticate<Operation::LOOKUP_PAIR, unsigned char*>, this },
      _backend{ BackendPath(_settings) }, _session{ _backend, _config.Get() } { }
auto Driver::_Poll(std::span<sdlrdp_event> events) const -> std::size_t {
  auto const count = Call<Operation::POLL>(events.data(), static_cast<unsigned>(events.size()));
  utilities::Ensures(count <= events.size(), "backend fills at most the event buffer");
  return count;
}
auto Driver::Options() const -> Settings const& {
  return _settings;
}
auto Driver::Config() const -> sdlrdp_config const& {
  return _config.Get();
}
auto Driver::AuthDisplay(SDL_PropertiesID properties) noexcept -> void {
  _auth_properties.store(properties);
}
auto Driver::_ReportError() const -> void {
  SDL_SetError("%s", _backend.Call<Operation::LAST_ERROR>());
}
auto Driver::Throw() const -> void {
  throw std::runtime_error(_backend.Call<Operation::LAST_ERROR>());
}
auto PublishAuthentication(Driver& driver, SDL_PropertiesID properties) -> DisplayAuthentication {
  utilities::Expects(properties != 0, "authentication reports to a display's properties");
  driver.AuthDisplay(properties);
  return { driver, properties };
}
auto WithdrawAuthentication(DisplayAuthentication const& authentication) noexcept -> void {
  authentication.first.get().AuthDisplay(0);
}
}
