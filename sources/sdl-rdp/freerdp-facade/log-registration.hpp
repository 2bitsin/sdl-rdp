#pragma once
#include <sdl-rdp/abi/backend.h>

#include <winpr/wlog.h>
#include <string>

namespace sdl_rdp::freerdp_facade::detail::log_registration {
// abi: sdlrdp_config registers a C callback with the void* user it is called back with; the pair travels together.
class LogRegistration {
public:
  explicit LogRegistration(sdlrdp_config const& config) noexcept;
  auto     operator()(sdlrdp_log_level level, std::string const& text) const    -> void;
  auto     operator()(sdlrdp_log_level level, wLogMessage const& message) const -> void;

private:
  decltype(sdlrdp_config::log)      _callback;
  decltype(sdlrdp_config::log_user) _user;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::log_registration::LogRegistration;
}
