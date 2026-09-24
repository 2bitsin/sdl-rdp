#include "support.test/sample-process.hpp"

#include "support.test/next-frame.hpp"
#include "support.test/sample-launch.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <freerdp/input.h>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp-backend.so/_detail/test-input-steps.hpp>
#include <utility>

namespace SampleGate {
auto SampleProcess::ThenInputEvent(std::string_view event, std::string_view text, unsigned& motion_frame) -> void {
  ASSERT_TRUE(Read("event " + std::string(event) + " ")) << event << text << ": " << process->Transcript();
  ASSERT_TRUE(line.contains(text)) << "expected " << event << text << ", actual: " << line;
  if (event == "MOUSE_MOTION") {
    motion_frame = utilities::Required(oxbox::utilities::ParseNumberAfter<unsigned>(line, " frame="),
                                       "motion events carry a frame identifier");
  }
}
auto SampleProcess::GivenProcess(std::vector<std::string> arguments) -> void {
  if (arguments.empty()) arguments = Arguments(certificates.Path(), false);
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
}
auto SampleProcess::GivenFocus(Client& client) -> void {
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
}
auto SampleProcess::WhenTextStops(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3c));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3c));
  ASSERT_TRUE(Read("event TEXT_MODE active=0"));
}

auto SampleProcess::WhenRelative(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
}
auto SampleProcess::SetUp() -> void {
  std::scoped_lock const lock(log_guard);
  client_logs = &logs;
  auto* root = WLog_GetRoot();
  ASSERT_NE(root, nullptr);
  wLogCallbacks callbacks{ CollectClientLog, CollectClientLog, CollectClientLog, CollectClientLog };
  ASSERT_TRUE(WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK));
  ASSERT_TRUE(WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks));
}
auto SampleProcess::ConnectLogs() -> std::string {
  // Drain the child pipe too: connect can fail before another Read consumes its diagnostics.
  std::string ignored;
  auto        deadline = Clock::now() + 10ms;
  if (process)
    while (process->Line(ignored, deadline)) {
    }
  return "\nclient:\n" + logs.Text(true) + "\nsample:\n" + (process ? process->Transcript() : "");
}
auto SampleProcess::Read(std::string_view expected, std::chrono::milliseconds timeout) -> bool {
  Expects(process != nullptr, "sample process is running");
  Expects(!expected.empty(), "expected output is nonempty");
  Expects(timeout > 0ms, "read timeout is positive");
  auto deadline = Clock::now() + timeout;
  while (process->Line(line, deadline))
    if (line.starts_with(expected)) return true;
  return false;
}
auto SampleProcess::ReadInput(Client& client, std::string_view expected, std::chrono::milliseconds timeout) -> bool {
  Expects(!expected.empty(), "expected input event supplied");
  bool received = false;
  return client.Until([&] { return received || (received = Read(expected, 1ms)); }, timeout);
}

auto SampleProcess::Exposed() -> void {
  ASSERT_TRUE(Read("event EXPOSED ")) << "EXPOSED missing: " << process->Transcript();
  auto name = line.find(" client_name=");
  ASSERT_NE(name, std::string::npos) << line;
  ASSERT_LT(name + 13, line.size()) << "non-empty client_name required: " << line;
}
auto SampleProcess::Input(Client& client) -> void {
  unsigned motion_frame = 0;
  Headless::SendKeyboardAndMouse(client, 100, 120);
  if (::testing::Test::HasFatalFailure()) return;
  for (auto [event, text] :
       std::array<std::pair<std::string_view, std::string_view>, 6>{ { { "KEY_DOWN"         , " scancode=4 " },
                                                                       { "KEY_UP"           , " scancode=4 " },
                                                                       { "MOUSE_MOTION"     , " x=100 y=120" },
                                                                       { "MOUSE_BUTTON_DOWN", " button=1 "   },
                                                                       { "MOUSE_BUTTON_UP"  , " button=1 "   },
                                                                       { "MOUSE_WHEEL"      , " y=1"         } } }) {
    ThenInputEvent(event, text, motion_frame);
    if (::testing::Test::HasFatalFailure()) return;
  }
  NextFrame frame(client, motion_frame);
  ASSERT_TRUE(client.Until([&] { return frame.Received(); })) << "next complete frame after motion";
  ASSERT_TRUE(frame.Matches()) << "frame excludes pointer: " << frame.Matches().message();
}
auto SampleProcess::TearDown() -> void {
  {
    std::scoped_lock const lock(log_guard);
    client_logs = nullptr;
  }
  if (process) SDL_Log("%s", process->Transcript().c_str());
}
auto SampleProcess::Escape(Client const& client) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 1))
      << "send Escape";
  ASSERT_TRUE(process->Exit()) << "sample exit 0 within ten seconds: " << process->Transcript();
}
auto SampleProcess::CollectClientLog(wLogMessage const* message) -> BOOL {
  std::scoped_lock const lock(log_guard);
  if (client_logs && message->TextString) {
    auto level = message->Level == WLOG_ERROR ? SDLRDP_LOG_ERROR
                 : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN
                                               : SDLRDP_LOG_INFO;
    Headless::Logs::Collect(client_logs, level, message->TextString);
  }
  return TRUE;
}
}
