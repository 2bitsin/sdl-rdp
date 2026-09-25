#include <sdl-rdp/sample-gate.test/process/captured-logs.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::sample_gate_test::process::detail::captured_logs {
using sdl_rdp::utilities::Expects;

auto LogLevel(SDL_LogPriority priority) -> sdlrdp_log_level {
  if (priority >= SDL_LOG_PRIORITY_ERROR) return SDLRDP_LOG_ERROR;
  return priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
}

CapturedLogs::CapturedLogs(Logs& logs, Capture capture) : logs(logs), forwarded(capture.forwarded) {
  SDL_GetLogOutputFunction(&previous, &previous_user);
  if (capture.video) {
    video = SDL_GetLogPriority(SDL_LOG_CATEGORY_VIDEO);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, *capture.video);
  }
  SDL_SetLogOutputFunction(Collect, this);
}
CapturedLogs::~CapturedLogs() {
  SDL_SetLogOutputFunction(previous, previous_user);
  if (video) SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, *video);
}
auto CapturedLogs::Collect(void* user, int category, SDL_LogPriority priority, char const* text) -> void {
  Expects(user != nullptr, "the log output names its capture");
  Expects(text != nullptr, "SDL logs a message");
  auto const& self = *static_cast<CapturedLogs*>(user);
  Logs::Collect(&self.logs, LogLevel(priority), text);
  if (self.forwarded && self.previous != nullptr) self.previous(self.previous_user, category, priority, text);
}
}
