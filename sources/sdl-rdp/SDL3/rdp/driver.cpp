#include "driver.hpp"
namespace sdl3::rdp::detail::driver {
Driver::Driver() : _config{ _options }, _credentials{ _config.Get() }, _backend{ _config.Get(), _log, _credentials } { }
auto Driver::Options() const noexcept -> sdl3::rdp::settings::Options const& {
  return _options;
}
auto Driver::Config() const noexcept -> sdl3::rdp::settings::Configuration const& {
  return _config;
}
auto Driver::Credentials() noexcept -> CredentialRelay& {
  return _credentials;
}
auto Driver::Backend() noexcept -> sdl_rdp::session::Backend& {
  return _backend;
}
}
