#include "_detail/logging.hpp"
#include "_detail/contract.hpp"
#include <winpr/wlog.h>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <stdexcept>

namespace Backend {
namespace {
// The newest open owns process-wide WLog routing; close waits for callbacks before clearing it.
std::atomic<LogRoute*> route = nullptr;
std::recursive_mutex routing_guard;
BOOL Forward(wLogMessage const* message)
{
  utilities::Expects(message != nullptr, "WLog message exists");
  std::scoped_lock lock(routing_guard);
  auto target = route.load();
  auto level = message->Level == WLOG_ERROR ? SDLRDP_LOG_ERROR
    : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
  if (target && target->callback && message->TextString)
    target->callback(target->user, level, message->TextString);
  return TRUE;
}
void Install()
{
  auto root = WLog_GetRoot();
  utilities::Expects(root != nullptr, "WLog root exists");
  wLogCallbacks callbacks{Forward, Forward, Forward, Forward};
  if (!WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)
      || !WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks))
    throw std::runtime_error("WLog callback installation failed.");
  if (auto level = std::getenv("WLOG_LEVEL")) WLog_SetStringLogLevel(root, level);
  else WLog_SetLogLevel(root, WLOG_WARN);
}
}
LogRoute::LogRoute(sdlrdp_config const& config) : callback(config.log), user(config.user)
{
  static std::once_flag installed;
  std::call_once(installed, Install);
  std::scoped_lock lock(routing_guard);
  route.store(this);
  utilities::Ensures(route.load() == this, "newest handle owns logging");
}
LogRoute::~LogRoute()
{
  std::scoped_lock lock(routing_guard);
  auto expected = this;
  route.compare_exchange_strong(expected, nullptr);
}
}
