#include "_detail/logging.hpp"
#include "_detail/contract.hpp"
#include <winpr/wlog.h>
#include <atomic>
#include <charconv>
#include <mutex>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <freerdp/error.h>

namespace Backend {
bool ExpectedDisconnect(unsigned code)
{
  return code == FREERDP_ERROR_LOGOFF_BY_USER || code == FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION
    || code == FREERDP_ERROR_RPC_INITIATED_DISCONNECT;
}
namespace {
// The newest open owns process-wide WLog routing; close waits for callbacks before clearing it.
std::atomic<LogRoute*> route = nullptr;
std::recursive_mutex routing_guard;
bool ExpectedLibraryMessage(wLogMessage const& message)
{
  if (!message.PrefixString || !message.TextString) return false;
  auto prefix = std::string_view(message.PrefixString);
  auto text = std::string_view(message.TextString);
  constexpr std::pair<std::string_view, std::string_view> known[] = {
    {"com.freerdp.core.transport", "BIO_read retries exceeded"},
    {"com.freerdp.channels.rdpsnd.server", "client doesn't support any format!"},
  };
  for (auto const& [source, message_text] : known)
    if (prefix == source && text == message_text) return true;
  if (prefix == "com.freerdp.core.transport") {
    constexpr std::string_view system_error = "BIO_read returned a system error ";
    if (!text.starts_with(system_error)) return false;
    text.remove_prefix(system_error.size());
    unsigned error = 0;
    auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), error);
    return status == std::errc{} && std::string_view(end, text.data() + text.size()).starts_with(": ");
  }
  auto name = text.substr(0, text.find(' '));
  if ((prefix == "com.freerdp.core" || prefix == "com.freerdp.core.peer")
      && name == "ERRCONNECT_CONNECT_TRANSPORT_FAILED") return true;
  return prefix == "com.freerdp.core.peer" && (name == "ERRINFO_LOGOFF_BY_USER"
    || name == "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION" || name == "ERRINFO_RPC_INITIATED_DISCONNECT");
}
BOOL Forward(wLogMessage const* message)
{
  utilities::Expects(message != nullptr, "WLog message exists");
  std::scoped_lock lock(routing_guard);
  auto target = route.load();
  auto level = message->Level == WLOG_ERROR ? SDLRDP_LOG_ERROR
    : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
  if (ExpectedLibraryMessage(*message)) level = SDLRDP_LOG_INFO;
  auto text = message->TextString;
  if (target && target->callback && text) target->callback(target->user, level, text);
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
  WLog_Layout_SetPrefixFormat(root, WLog_GetLogLayout(root), "%mn");
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
