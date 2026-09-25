#include "log-relay.hpp"
#include <sdl-rdp/SDL3/rdp/sdl/internals.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <string>

namespace sdl3::rdp::detail::log_relay {
using sdl_rdp::utilities::Unreachable;
namespace {
auto Priority(LogLevel level) -> SDL_LogPriority {
  switch (level) {
  case LogLevel::Error: return SDL_LOG_PRIORITY_ERROR;
  case LogLevel::Warn:  return SDL_LOG_PRIORITY_WARN;
  case LogLevel::Info:  return SDL_LOG_PRIORITY_INFO;
  default:              Unreachable(level);
  }
}
}
auto LogRelay::Log(LogLevel level, std::string_view text) -> void {
  std::string const line{ text };
  SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, Priority(level), "%s", line.c_str());
}
}
