#include "support.test/sample-session.hpp"

#include "support.test/client-steps.hpp"
#include "support.test/sample-launch.hpp"

#include <utility>

namespace SampleGate {
auto SampleSession::GivenDesktopProcess(std::vector<std::string> const& arguments) -> void {
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  session = std::make_unique<Client>(AnnouncedPort(line), true, 1280, 800);
  ConnectExposed(*session);
}
auto SampleSession::GivenDriveProcess(std::vector<std::string> const& arguments, fs::path const& share) -> void {
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
  ConnectDrive(*session, share);
}
auto SampleSession::ConnectExposed(Client& client) -> void {
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  Exposed();
}
auto SampleSession::GivenInputSession(bool advanced) -> void {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
  if (advanced) channels = std::make_unique<InputClient>(*session);
  GivenFocus(*session);
}
auto SampleSession::SessionClient() -> Client& {
  return *session;
}
auto SampleSession::GivenPositionSession() -> void {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(session->Instance().get()));
  position = std::make_unique<PositionObserver>(*session);
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
}
auto SampleSession::Position() -> PositionObserver& {
  return *position;
}
auto SampleSession::GivenFullscreen() -> void {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200" });
  GivenProcess(arguments);
}
auto SampleSession::GivenAspect() -> void {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--size", "640x350", "--aspect", "4:3" });
  GivenProcess(arguments);
}
auto SampleSession::ThenExplicitGeometry(Client& client, unsigned height) -> void {
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] {
    auto* gdi = client.Instance()->context->gdi;
    return gdi->width == 320 && std::cmp_equal(gdi->height, height);
  }));
}
auto SampleSession::GivenAudioProcess(std::vector<std::string> const& arguments) -> void {
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  audio_port = AnnouncedPort(line);
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
}
auto SampleSession::ThenIniConnects(std::vector<std::string> const& args, unsigned port) -> void {
  GivenIniProcess(args, port);
  if (::testing::Test::HasFatalFailure()) return;
  Client const client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  Escape(client);
}
auto SampleSession::ClipboardSession() -> Headless::ClipboardClient& {
  return *clipboard;
}
auto SampleSession::GivenClipboard(std::string const& text) -> void {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--clip", text });
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  session   = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
  clipboard = std::make_unique<Headless::ClipboardClient>(*session);
  ASSERT_TRUE(freerdp_connect(session->Instance().get())) << ConnectLogs();
}
auto SampleSession::GivenIniProcess(std::vector<std::string> const& args, unsigned port) -> void {
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("port ")) << process->Transcript();
  EXPECT_EQ(AnnouncedPort(line), port);
}
}
