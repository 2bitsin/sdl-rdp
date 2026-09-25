#include "configured-text.hpp"
namespace sdl3::rdp::settings::detail::configured_text {
using sdl_rdp::settings::Settings;

ConfiguredText::ConfiguredText(Options const& options)
    : _bind{ options.Get<&Settings::bind>() }, _cert_dir{ options.Get<&Settings::cert_dir>() },
      _user{ options.Get<&Settings::user>() }, _password{ options.Get<&Settings::password>() },
      _domain{ options.Get<&Settings::domain>() } { }
auto ConfiguredText::Fill(sdlrdp_config& config) const -> void {
  config.bind     = _bind ? _bind->c_str() : nullptr;
  config.cert_dir = _cert_dir ? _cert_dir->c_str() : nullptr;
  config.user     = _user ? _user->c_str() : nullptr;
  config.password = _password ? _password->c_str() : nullptr;
  config.domain   = _domain ? _domain->c_str() : nullptr;
}
}
