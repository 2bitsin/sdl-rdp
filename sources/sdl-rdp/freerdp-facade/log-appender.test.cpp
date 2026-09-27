#include <sdl-rdp/freerdp-facade/log-appender.hpp>

#include <gtest/gtest.h>
#include <winpr/wlog.h>
#include <string>
#include <tuple>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::log_appender {
namespace {
using Seen = std::vector<std::tuple<LogSeverity, std::string, std::string>>;
auto Recorded() -> Seen& {
  static Seen seen;
  return seen;
}
}
TEST(LogAppender, HandsTheTargetEachTextMessageAtInfoOrAbove) {
  InstallLogAppender(
      [](LogMessage const& message) { Recorded().emplace_back(message.severity, message.prefix, message.text); });
  auto* log = WLog_Get("com.sdl-rdp.appender");
  ASSERT_NE(log, nullptr);
  ASSERT_TRUE(WLog_SetLogLevel(log, WLOG_TRACE));
  WLog_Print(log, WLOG_DEBUG, "below info");
  WLog_Print(log, WLOG_INFO, "info %d", 1);
  WLog_Print(log, WLOG_ERROR, "error");
  EXPECT_EQ(Recorded(), (Seen{ { LogSeverity::Info , "com.sdl-rdp.appender", "info 1" },
                               { LogSeverity::Error, "com.sdl-rdp.appender", "error"  } }));
}
TEST(LogAppenderDeathTest, InstallsOncePerProcess) {
  EXPECT_DEATH(
      {
        InstallLogAppender([](LogMessage const&) { });
        InstallLogAppender([](LogMessage const&) { });
      },
      "installed once per process");
}
}
