#include "support.test/client-steps.hpp"
#include "support.test/first-frame-size.hpp"
#include "support.test/frame-pattern.hpp"
#include "support.test/pointer-observer.hpp"
#include "support.test/process.hpp"
#include "support.test/procfs.hpp"
#include "support.test/sample-launch.hpp"
#include "support.test/sample.hpp"

#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <sdl-rdp-backend.so/_detail/avc-encoder.hpp>
#include <sdl-rdp-backend.so/_detail/display-client.hpp>
#include <string>
#include <thread>
#include <vector>

namespace SampleGate {
namespace {
class DesktopSample : public SampleGate::Sample {
protected:
  void ThenLegacyClipboard(Client& client) {
    ASSERT_EQ(ClipboardSession().RequestFormat(CF_TEXT), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received({ '?', '?', '?', 'w', 0 }); }));
    SDL_Log("trace CLIPBOARD server request=1 bytes=3f3f3f7700 text=???w");
    ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  }
  void GivenFocusedClient(Client const& first) {
    ASSERT_TRUE(freerdp_connect(first.Instance().get())) << ConnectLogs();
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
    ASSERT_TRUE(Read("event MOUSE_ENTER "));
  }
};
TEST_F(DesktopSample, WholeSystem) {
  GivenWholeSample();
  if (::testing::Test::HasFatalFailure()) return;
  auto port = AnnouncedPort(line);
  ASSERT_GT(port, 0u) << line;
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs() << "connect 640x480";
  Exposed();
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(Read("event FOCUS_GAINED ")) << "FOCUS_GAINED: " << process->Transcript();
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }))
      << "0x010101 background and one green 32x32 block: " << Pattern(client, false).message();
  Input(client);
  if (::testing::Test::HasFatalFailure()) return;
  WhenWholeSampleReconnects(client, port);
}

TEST_F(DesktopSample, RequestedSizeReturns) {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  auto   port  = AnnouncedPort(line);
  Client first(port, true, 320, 200);
  WhenSmallerDesktop(first);
  if (::testing::Test::HasFatalFailure()) return;
  Client second(port, true, 800, 600);
  ASSERT_TRUE(freerdp_connect(second.Instance().get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=800x600"));
  ASSERT_TRUE(second.Until([&] { return Pattern(second, false); }));
  SDL_Log("%s", process->Transcript().c_str());
  Escape(second);
}

TEST_F(DesktopSample, TakeoverFocus) {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  auto         port  = AnnouncedPort(line);
  Client const first(port, true, 640, 480);
  GivenFocusedClient(first);
  if (::testing::Test::HasFatalFailure()) return;
  Client const second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.Instance().get())) << ConnectLogs();
  for (auto const* expected : { "OCCLUDED", "FOCUS_LOST", "MOUSE_LEAVE", "EXPOSED", "FOCUS_GAINED", "MOUSE_ENTER" }) {
    ThenTakeoverEvent(expected);
    if (::testing::Test::HasFatalFailure()) return;
  }
  SDL_Log("%s", process->Transcript().c_str());
  Escape(second);
}

TEST_F(DesktopSample, AutoAvcCodecProperty) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=auto");
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client client(AnnouncedPort(line), true, 640, 480);
  client.EnableGraphics(true);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  ASSERT_TRUE(ReadInput(client, "event CODEC_CHANGED codec=avc420", 30s)) << process->Transcript();
  Escape(client);
}

TEST_F(DesktopSample, LiveCodec) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=remotefx");
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client const client(AnnouncedPort(line), true, 640, 480);
  GivenSwitchableCodec(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(Read("event EXPOSED "));
  ASSERT_TRUE(line.ends_with("codec=remotefx")) << line;
  WhenCodecKeyChanges(client);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, WaitForClient) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), true));
  auto     deadline = Clock::now() + 10s;
  unsigned port     = 0;
  while (!(port = ListeningPort()) && Clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  ASSERT_GT(port, 0u) << "sample's ephemeral listener: " << process->Transcript();
  ASSERT_FALSE(Read("port ", 300ms)) << "no port line before client: " << process->Transcript();
  Client const client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs() << "connect to waiting sample";
  ThenWaitingPort(port);
  if (::testing::Test::HasFatalFailure()) return;
  Exposed();
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}
TEST_F(DesktopSample, DesktopIsPicture) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--size", "640x480" });
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client client(AnnouncedPort(line), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=1024x768"));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  Escape(client);
}

TEST_F(DesktopSample, FullscreenFollowsScreen) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, { "SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480" });
  arguments.emplace_back("--fullscreen");
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client                        client(AnnouncedPort(line), true, 1024, 768);
  Headless::DisplayClient const display(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  ThenSizeEvents("data1=1024 data2=768");
  if (::testing::Test::HasFatalFailure()) return;
  ChangeMonitor(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] {
    auto gdi = client.Instance()->context->gdi;
    return gdi->width == 1920 && gdi->height == 1080;
  }));
  ThenSizeEvents("data1=1920 data2=1080");
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, FirstFrameObserverWithoutSuccessfulConnect) {
  Client client(0, true);
  auto   paint   = +[](rdpContext*) -> BOOL { return TRUE; };
  auto   connect = +[](freerdp*) -> BOOL { return FALSE; };
  client.Instance()->context->update->EndPaint = paint;
  client.Instance()->PostConnect               = connect;
  for (bool const attempt : { false, true }) {
    {
      FirstFrameSize const frame(client);
      if (attempt) EXPECT_FALSE(client.Instance()->PostConnect(client.Instance().get()));
    }
    EXPECT_EQ(client.Instance()->context->update->EndPaint, paint);
    EXPECT_EQ(client.Instance()->PostConnect, connect);
  }
}

TEST_F(DesktopSample, WindowResizeMovesDesktopMode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, { "SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800" });
  arguments.insert(arguments.end(), { "--size", "1280x800" });
  GivenDesktopProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x40));
  ThenDesktopMode(client, 1920, 1080);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, FullscreenModeMovesDesktopMode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, { "SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800" });
  arguments.insert(arguments.end(), { "--size", "1280x800", "--mode", "1920x1080" });
  GivenDesktopProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  PressFullscreenKey(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenDesktopMode(client, 1920, 1080);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x3e));
  ThenDesktopMode(client, 1280, 800);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, CursorShape) {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  Client client(AnnouncedPort(line), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  PointerObserver pointer(client);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(client.Until([&] { return pointer.Red() && Pattern(client, false); }));
  SDL_Log("event POINTER width=8 height=8 argb=ffff0000 frame_has_no_red_block=1");
  Escape(client);
}

TEST_F(DesktopSample, Soname) {
  auto library = BuildRoot() / "sources/SDL3.so/libSDL3.so.0";
  ASSERT_TRUE(fs::is_regular_file(library));
  process = std::make_unique<Process>(std::vector<std::string>{ "env", "objdump", "-p", library.string() });
  bool found = false;
  while (process->Line(line, Clock::now() + 10s)) {
    if (line.find("SONAME") == std::string::npos) continue;
    EXPECT_TRUE(line.ends_with("libSDL3.so.0")) << line;
    SDL_Log("%s", line.c_str());
    found = true;
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(process->Exit());
}

TEST_F(DesktopSample, ClipboardAscii) {
  GivenClipboard("hello");
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  ASSERT_TRUE(
      client.Until([&] { return ClipboardSession().Received({ 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 0, 0 }); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=680065006c006c006f000000 text=hello");
  ASSERT_EQ(ClipboardSession().RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received({ 'h', 'e', 'l', 'l', 'o', 0 }); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=68656c6c6f00 text=hello");
  WhenAsciiClipboardOffered(client);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, ClipboardUnicode) {
  GivenClipboard("żółw");
  if (::testing::Test::HasFatalFailure()) return;
  auto&             client = SessionClient();
  std::vector<BYTE> bytes  { 0x7c, 1, 0xf3, 0, 0x42, 1, 0x77, 0, 0, 0 };
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received(bytes); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=7c01f300420177000000 text=żółw");
  ThenLegacyClipboard(client);
  if (::testing::Test::HasFatalFailure()) return;
  WhenClipboardEmptied(client);
  if (::testing::Test::HasFatalFailure()) return;
  WhenUnicodeClipboardOffered(client, bytes);
  if (::testing::Test::HasFatalFailure()) return;
  ThenClipboardCleared(client, ClipboardSession());
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(DesktopSample, GraphicsPipelinePattern) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_LOGGING=video=info");
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client client(AnnouncedPort(line), true, 640, 480);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << Pattern(client, false).message();
  ASSERT_TRUE(Read("GFX advertised")) << process->Transcript();
  RecordProperty("trace", process->Transcript());
  Escape(client);
}
TEST_F(DesktopSample, InvalidCodecLogsValidNames) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=avc");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read(
      "ERROR: Invalid SDL_RDP_CODEC 'avc'; valid names: auto, planar, remotefx, nscodec, raw, progressive, avc420"))
      << process->Transcript();
}
TEST_F(DesktopSample, MalformedSizeIsRefused) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--size", "640x" });
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("ERROR: Invalid size '640x': expected WxH with positive sides")) << process->Transcript();
}

}
}
