#pragma once
#include "sdl-rdp-backend.h"
namespace Backend {
bool ExpectedDisconnect(unsigned code);
struct LogRoute {
  explicit LogRoute(sdlrdp_config const& config);
  ~LogRoute();
  void (*callback)(void*, sdlrdp_log_level, const char*);
  void* user;
};
}
