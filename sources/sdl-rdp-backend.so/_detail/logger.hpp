#pragma once
#include "logging.hpp"
#include "sdl-rdp-backend.h"

#include <string>

namespace Backend {
class Logger {
public:
  explicit Logger(sdlrdp_config const& config);
  auto     Log(sdlrdp_log_level level, std::string const& text) const -> void;

private:
  using Callback = auto (*)(void*, sdlrdp_log_level, char const*) -> void;
  LogRoute _route;
  Callback _callback;
  void*    _user;
};
}
