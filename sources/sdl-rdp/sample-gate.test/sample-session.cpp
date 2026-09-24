#include <sdl-rdp/sample-gate.test/sample-session.hpp>

#include <sdl-rdp/sample-gate.test/client-steps.hpp>
#include <sdl-rdp/sample-gate.test/frame-pattern.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

#include <utility>

namespace SampleGate {
auto SampleSession::GivenDesktopProcess(Words const& environment, Words const& options) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession(environment, options, 1280, 800));
  ConnectExposed(*session);
}
auto SampleSession::GivenDriveProcess(Words const& options, fs::path const& share) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession({ }, options));
  ConnectDrive(*session, share);
}
auto SampleSession::ConnectExposed(Client& client) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  Exposed();
}
auto SampleSession::GivenInputSession(bool advanced) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession());
  if (advanced) channels = std::make_unique<InputClient>(*session);
  GivenFocus(*session);
}
auto SampleSession::SessionClient() -> Client& {
  return *session;
}
auto SampleSession::GivenPositionSession() -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession());
  ASSERT_NO_FATAL_FAILURE(Connect(*session));
  position = std::make_unique<PositionObserver>(*session);
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
}
auto SampleSession::Position() -> PositionObserver& {
  return *position;
}
auto SampleSession::GivenFullscreen() -> void {
  GivenProcess({ }, { "--fullscreen", "--mode", "320x200" });
}
auto SampleSession::ThenExplicitGeometry(Client& client, unsigned height) -> void {
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.UntilDesktop(320, height));
}
auto SampleSession::GivenAudioProcess(Words const& environment, Words const& options) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenProcess(environment, options));
  audio_port = AnnouncedPort(line);
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
}
auto SampleSession::ThenIniConnects(std::vector<std::string> const& args, unsigned port) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenIniProcess(args, port));
  Client client(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  Escape(client);
}
auto SampleSession::ClipboardSession() -> Headless::ClipboardClient& {
  return *clipboard;
}
auto SampleSession::GivenClipboard(std::string const& text) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession({ }, { "--clip", text }));
  clipboard = std::make_unique<Headless::ClipboardClient>(*session);
  ASSERT_NO_FATAL_FAILURE(Connect(*session));
}
auto SampleSession::GivenSession(Words const& environment, Words const& options, std::uint32_t width,
                                 std::uint32_t height) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenProcess(environment, options));
  session = std::make_unique<Client>(AnnouncedPort(line), true, width, height);
}
auto SampleSession::GivenIniProcess(std::vector<std::string> const& args, unsigned port) -> void {
  ASSERT_NO_FATAL_FAILURE(Launch(args));
  EXPECT_EQ(AnnouncedPort(line), port);
}
}
