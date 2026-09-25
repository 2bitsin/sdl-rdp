#include <sdl-rdp/freerdp-facade/log-registration.hpp>

namespace sdl_rdp::freerdp_facade::detail::log_registration {
LogRegistration::LogRegistration(sdlrdp_config const& config) noexcept
    : _callback{ config.log }, _user{ config.log_user } { }
auto LogRegistration::operator()(sdlrdp_log_level level, std::string const& text) const -> void {
  if (_callback) _callback(_user, level, text.c_str());
}
auto LogRegistration::operator()(sdlrdp_log_level level, wLogMessage const& message) const -> void {
  if (_callback && message.TextString) _callback(_user, level, message.TextString);
}
}
