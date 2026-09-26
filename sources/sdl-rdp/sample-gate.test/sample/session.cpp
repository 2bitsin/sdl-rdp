#include <sdl-rdp/sample-gate.test/sample/session.hpp>

#include <sdl-rdp/sample-gate.test/client/steps.hpp>
#include <sdl-rdp/sample-gate.test/frame/pattern.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace sdl_rdp::sample_gate_test::sample::detail::session {
using sdl_rdp::headless_client_test::client::UntilDesktop;
using sdl_rdp::sample_gate_test::client::ConnectDrive;
using sdl_rdp::sample_gate_test::frame::Pattern;

auto SampleSession::GivenDesktopProcess(Words const& environment, Words const& options) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession(environment, options, 1280, 800));
  ConnectExposed(*session);
}
auto SampleSession::GivenDriveProcess(Words const& options, std::filesystem::path const& share) -> void {
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
auto SampleSession::ThenExplicitGeometry(Client& client, std::uint32_t height) -> void {
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(UntilDesktop(client, 320, height));
}
auto SampleSession::GivenAudioProcess(Words const& environment, Words const& options) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenProcess(environment, options));
  audio_port = AnnouncedPort(line);
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
}
auto SampleSession::ThenSettingsConnect(std::vector<std::string> const& args, std::uint32_t port,
                                        std::string_view codec) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSettingsProcess(args, port));
  Client client(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(Read("event EXPOSED ")) << process->Transcript();
  EXPECT_TRUE(line.ends_with(std::format(" codec={}", codec))) << line;
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  Escape(client);
}
auto SampleSession::ClipboardSession() -> ClipboardClient& {
  return *clipboard;
}
auto SampleSession::GivenClipboard(std::string const& text) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenSession({ }, { "--clip", text }));
  clipboard = std::make_unique<ClipboardClient>(*session);
  ASSERT_NO_FATAL_FAILURE(Connect(*session));
}
auto SampleSession::GivenSession(Words const& environment, Words const& options, std::uint32_t width,
                                 std::uint32_t height) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenProcess(environment, options));
  session = std::make_unique<Client>(AnnouncedPort(line), true, width, height);
}
auto SampleSession::GivenSettingsProcess(std::vector<std::string> const& args, std::uint32_t port) -> void {
  ASSERT_NO_FATAL_FAILURE(Launch(args));
  EXPECT_EQ(AnnouncedPort(line), port);
}
}
