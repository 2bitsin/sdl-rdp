#include <sdl-rdp/sample-gate.test/sample/process.hpp>

#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <SDL3/SDL.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::sample::detail::process {
using sdl_rdp::diagnostics::LogLevel;
using namespace std::chrono_literals;

auto SampleProcess::GivenProcess(Words const& environment, Words const& options) -> void {
  Launch(Arguments(certificates.Path(), environment, options));
}
auto SampleProcess::Launch(Words const& arguments) -> void {
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port ")) << process->Transcript();
}
auto SampleProcess::AnnouncedClient(std::uint32_t width, std::uint32_t height) -> Client {
  return Client(AnnouncedPort(line), true, width, height);
}
auto SampleProcess::Connect(Client& client) -> void {
  ASSERT_TRUE(client.Connect()) << ConnectLogs();
}
auto SampleProcess::ConnectAcknowledging(Client& client) -> void {
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, 2));
  Connect(client);
}
auto SampleProcess::SetUp() -> void {
  client_logs.Publish(logs);
  auto* root = WLog_GetRoot();
  ASSERT_NE(root, nullptr);
  // abi: wLogCallbackMessage_t and its siblings, BOOL is int
  constexpr auto collect   = [](wLogMessage const* message) -> int {
    Expects(message != nullptr, "the appender is handed its message");
    CollectClientLog(*message);
    return true;
  };
  wLogCallbacks  callbacks { collect, collect, collect, collect };
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

auto SampleProcess::Exposed() -> void {
  ASSERT_TRUE(Read("event EXPOSED ")) << "EXPOSED missing: " << process->Transcript();
  auto name = line.find(" client_name=");
  ASSERT_NE(name, std::string::npos) << line;
  ASSERT_LT(name + 13, line.size()) << "non-empty client_name required: " << line;
}
auto SampleProcess::TearDown() -> void {
  client_logs.Withdraw();
  if (process) SDL_Log("%s", process->Transcript().c_str());
}
auto SampleProcess::Escape(Client& client) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 1)) << "send Escape";
  ASSERT_TRUE(process->Exit()) << "sample exit 0 within ten seconds: " << process->Transcript();
}
auto SampleProcess::CollectClientLog(wLogMessage const& message) -> void {
  auto const collecting = client_logs.Peek();
  if (!collecting || message.TextString == nullptr) return;
  auto const level = message.Level == WLOG_ERROR ? LogLevel::Error
                     : message.Level == WLOG_WARN ? LogLevel::Warn
                                                  : LogLevel::Info;
  collecting->get().Log(level, message.TextString);
}
}
