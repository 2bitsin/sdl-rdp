#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::log_appender {
enum class LogSeverity : std::uint8_t { Info, Warn, Error, Fatal };
// A WLog text message at info or above; the prefix is the name of the logger that wrote it.
struct LogMessage {
  LogSeverity      severity{ };
  std::string_view prefix;
  std::string_view text;
};
using LogTarget = std::function<void(LogMessage const&)>;
// Once per process: WLog's appender takes no user data, so the target is the process's.
auto InstallLogAppender(LogTarget target) -> void;
}

namespace sdl_rdp::freerdp_facade {
using detail::log_appender::InstallLogAppender;
using detail::log_appender::LogMessage;
using detail::log_appender::LogSeverity;
using detail::log_appender::LogTarget;
}
