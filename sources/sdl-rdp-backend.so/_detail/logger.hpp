#pragma once
#include "logging.hpp"
#include "sdl-rdp-backend.h"

#include <string>

namespace Backend {
class Logger {
public:
  explicit Logger(sdlrdp_config const& config);
  void     Log(sdlrdp_log_level level, std::string const& text) const;

private:
  using Callback = void (*)(void*, sdlrdp_log_level, char const*);
  LogRoute _route;
  Callback _callback;
  void*    _user;
};
}
