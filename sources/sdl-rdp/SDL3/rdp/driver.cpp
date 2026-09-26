#include "driver.hpp"
#include <sdl-rdp/SDL3/rdp/settings/configuration.hpp>
namespace sdl3::rdp::detail::driver {
Driver::Driver()
    : _config{ sdl3::rdp::settings::SetupFrom(_options) }, _credentials{ _config },
      _backend{ _config, _log, _credentials } { }
auto Driver::Options() const noexcept -> sdl3::rdp::settings::Options const& {
  return _options;
}
auto Driver::Config() const noexcept -> sdl_rdp::configuration::Setup const& {
  return _config;
}
auto Driver::Credentials() noexcept -> CredentialRelay& {
  return _credentials;
}
auto Driver::Backend() noexcept -> sdl_rdp::session::Backend& {
  return _backend;
}
}
