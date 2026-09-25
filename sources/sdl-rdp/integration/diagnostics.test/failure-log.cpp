#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <gtest/gtest.h>
#include <stdexcept>

namespace sdl_rdp::integration::diagnostics_test::detail::failure_log {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::utilities::Contained;
namespace {
auto Collected(Logs& logs) -> sdlrdp_config {
  return { .log = Logs::Collect, .log_user = &logs };
}
}
TEST(FailureLog, LogsTheOperationAndTheFailureAtItsLevel) {
  Logs              logs;
  Diagnostics const diagnostics{ Collected(logs), false };
  FailureLog{ diagnostics, "Clipboard text decoding", SDLRDP_LOG_WARN }("invalid UTF-16");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Clipboard text decoding failed: invalid UTF-16.")) << logs.Text(true);
}
TEST(FailureLog, CarriesAContainedCallbackFailureToTheDiagnosticsLog) {
  Logs              logs;
  Diagnostics const diagnostics { Collected(logs), false };
  auto const        failing     = []() -> int { throw std::runtime_error("peer vanished"); };
  EXPECT_EQ(Contained(-1, failing, FailureLog{ diagnostics, "Peer activation" }), -1);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "Peer activation failed: peer vanished.")) << logs.Text(true);
}
}
