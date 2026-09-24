#include <sdl-rdp/core/failure-log.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/headless-client.test/logs.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <gtest/gtest.h>
#include <stdexcept>

namespace {
auto Collected(Headless::Logs& logs) -> sdlrdp_config {
  return { .log = Headless::Logs::Collect, .log_user = &logs };
}
}
TEST(FailureLog, LogsTheOperationAndTheFailureAtItsLevel) {
  Headless::Logs             logs;
  Backend::Diagnostics const diagnostics{ Collected(logs), false };
  Backend::FailureLog{ diagnostics, "Clipboard text decoding", SDLRDP_LOG_WARN }("invalid UTF-16");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Clipboard text decoding failed: invalid UTF-16.")) << logs.Text(true);
}
TEST(FailureLog, CarriesAContainedCallbackFailureToTheDiagnosticsLog) {
  Headless::Logs             logs;
  Backend::Diagnostics const diagnostics { Collected(logs), false };
  auto const                 failing     = []() -> int { throw std::runtime_error("peer vanished"); };
  EXPECT_EQ(Backend::Contained(-1, failing, Backend::FailureLog{ diagnostics, "Peer activation" }), -1);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "Peer activation failed: peer vanished.")) << logs.Text(true);
}
