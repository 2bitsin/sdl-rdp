#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/diagnostics/logging.hpp>

#include <string>

namespace sdl_rdp::diagnostics::detail::logger {
class Logger {
public:
  explicit Logger(sdlrdp_config const& config);
  auto     Log(sdlrdp_log_level level, std::string const& text) const -> void;

private:
  LogRoute _route;
};
}

namespace sdl_rdp::diagnostics {
using detail::logger::Logger;
}
