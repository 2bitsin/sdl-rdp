#include <sdl-rdp/freerdp-facade/log-appender.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/wlog.h>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::log_appender {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

namespace {
auto Target() -> std::optional<LogTarget>& {
  // WLog has no user pointer; the target lasts as long as the process-wide appender.
  static std::optional<LogTarget> target;
  return target;
}
auto Severity(std::uint32_t level) -> LogSeverity {
  switch (level) {
  case WLOG_INFO:  return LogSeverity::Info;
  case WLOG_WARN:  return LogSeverity::Warn;
  case WLOG_ERROR: return LogSeverity::Error;
  case WLOG_FATAL: return LogSeverity::Fatal;
  default:         Unreachable(level);
  }
}
auto Forward(wLogMessage const& message) -> void {
  if (message.Level < WLOG_INFO || message.Level > WLOG_FATAL) return;
  if (message.Type != WLOG_MESSAGE_TEXT || !message.TextString) return;
  auto const  prefix = message.PrefixString ? std::string_view{ message.PrefixString } : std::string_view{ };
  auto const& target = Target();
  // A failed install resets the target while WLog may keep the callback it was given.
  if (!target) return;
  std::invoke(*target, LogMessage{ .severity = Severity(message.Level), .prefix = prefix, .text = message.TextString });
}
auto Configure(wLog& root) -> bool {
  // abi: wLogCallbackMessage_t and its siblings, whose BOOL differs by platform
  constexpr auto forward   =
      [](wLogMessage const* message) noexcept -> std::invoke_result_t<wLogCallbackMessage_t, wLogMessage const*> {
    Expects(message != nullptr, "WLog message exists");
    // The log is the only reporting channel, so a failure to forward has nowhere further to go.
    auto const forwarded = [&] {
      Forward(*message);
      return true;
    };
    return Contained(false, forwarded, [](std::string_view) noexcept { });
  };
  wLogCallbacks  callbacks { forward, forward, forward, forward };
  return WLog_SetLogAppenderType(&root, WLOG_APPENDER_CALLBACK)
         && WLog_ConfigureAppender(WLog_GetLogAppender(&root), "callbacks", &callbacks)
         && WLog_Layout_SetPrefixFormat(&root, WLog_GetLogLayout(&root), "%mn");
}
// An unknown WLOG_LEVEL keeps WLog's level: a log setting never stops the driver.
auto Filter(wLog& root) -> void {
  if (auto const* level = std::getenv("WLOG_LEVEL"))
    std::ignore = WLog_SetStringLogLevel(&root, level);
  else
    std::ignore = WLog_SetLogLevel(&root, WLOG_WARN);
}
}
auto InstallLogAppender(LogTarget target) -> void {
  auto& installed = Target();
  Expects(!installed.has_value(), "WLog's appender is installed once per process");
  Expects(static_cast<bool>(target), "the log target is callable");
  auto* root = WLog_GetRoot();
  if (root == nullptr) throw LibraryInitFailed{ "WLog root" };
  installed = std::move(target);
  if (!Configure(*root)) {
    installed.reset();
    throw LibraryInitFailed{ "WLog appender" };
  }
  Filter(*root);
}
}
