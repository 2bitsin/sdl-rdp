#include <sdl-rdp/sample-gate.test/client/pointer-observer.hpp>
#include <sdl-rdp/sample-gate.test/client/steps.hpp>
#include <sdl-rdp/sample-gate.test/frame/first-size.hpp>
#include <sdl-rdp/sample-gate.test/frame/pattern.hpp>
#include <sdl-rdp/sample-gate.test/process/process.hpp>
#include <sdl-rdp/sample-gate.test/process/procfs.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace SampleGate {
namespace {
auto DesktopSize() -> Words {
  return { "SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800" };
}
class DesktopSample : public SampleGate::Sample {
protected:
  auto ThenLegacyClipboard(Client& client) -> void {
    ASSERT_EQ(ClipboardSession().RequestFormat(CF_TEXT), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received({ '?', '?', '?', 'w', 0 }); }));
    SDL_Log("trace CLIPBOARD server request=1 bytes=3f3f3f7700 text=???w");
    ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  }
  auto GivenFocusedClient(Client& first) -> void {
    ASSERT_NO_FATAL_FAILURE(Connect(first));
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
    ASSERT_TRUE(Read("event MOUSE_ENTER "));
  }
};
TEST_F(DesktopSample, WholeSystem) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto port = AnnouncedPort(line);
  ASSERT_GT(port, 0u) << line;
  Client client(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event FOCUS_GAINED ")) << "FOCUS_GAINED: " << process->Transcript();
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }))
      << "0x010101 background and one green 32x32 block: " << Pattern(client, false).message();
  ASSERT_NO_FATAL_FAILURE(Input(client));
  WhenWholeSampleReconnects(client, port);
}

TEST_F(DesktopSample, RequestedSizeReturns) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto   port  = AnnouncedPort(line);
  Client first(port, true, 320, 200);
  ASSERT_NO_FATAL_FAILURE(WhenSmallerDesktop(first));
  Client second(port, true, 800, 600);
  ASSERT_NO_FATAL_FAILURE(Connect(second));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=800x600"));
  ASSERT_TRUE(second.Until([&] { return Pattern(second, false); }));
  SDL_Log("%s", process->Transcript().c_str());
  Escape(second);
}

TEST_F(DesktopSample, TakeoverFocus) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto   port  = AnnouncedPort(line);
  Client first(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(GivenFocusedClient(first));
  Client second(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(second));
  for (auto const* expected : { "OCCLUDED", "FOCUS_LOST", "MOUSE_LEAVE", "EXPOSED", "FOCUS_GAINED", "MOUSE_ENTER" }) {
    ASSERT_NO_FATAL_FAILURE(ThenTakeoverEvent(expected));
  }
  SDL_Log("%s", process->Transcript().c_str());
  Escape(second);
}

TEST_F(DesktopSample, AutoAvcCodecProperty) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ "SDL_RDP_CODEC=auto" }));
  auto client = AnnouncedClient(640, 480);
  client.EnableGraphics({ .h264 = true });
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(ReadInput(client, "event CODEC_CHANGED codec=avc420", 30s)) << process->Transcript();
  Escape(client);
}

TEST_F(DesktopSample, LiveCodec) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ "SDL_RDP_CODEC=remotefx" }));
  auto client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(GivenSwitchableCodec(client));
  ASSERT_TRUE(Read("event EXPOSED "));
  ASSERT_TRUE(line.ends_with("codec=remotefx")) << line;
  ASSERT_NO_FATAL_FAILURE(WhenCodecKeyChanges(client));
  Escape(client);
}

TEST_F(DesktopSample, WaitForClient) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), { "SDL_RDP_WAIT_FOR_CLIENT=1" }));
  auto          deadline = Clock::now() + 10s;
  std::uint32_t port     = 0;
  while (!(port = ListeningPort()) && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
  ASSERT_GT(port, 0u) << "sample's ephemeral listener: " << process->Transcript();
  ASSERT_FALSE(Read("port ", 300ms)) << "no port line before client: " << process->Transcript();
  Client client(port, true, 640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_NO_FATAL_FAILURE(ThenWaitingPort(port));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  Escape(client);
}
TEST_F(DesktopSample, DesktopIsPicture) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, { "--size", "640x480" }));
  auto client = AnnouncedClient(1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=1024x768"));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  Escape(client);
}

TEST_F(DesktopSample, FullscreenFollowsScreen) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ "SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480" }, { "--fullscreen" }));
  auto                          client  = AnnouncedClient(1024, 768);
  Headless::DisplayClient const display(client);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_NO_FATAL_FAILURE(ThenSizeEvents("data1=1024 data2=768"));
  ASSERT_NO_FATAL_FAILURE(ChangeMonitor(client));
  ASSERT_TRUE(client.UntilDesktop(1920, 1080));
  ASSERT_NO_FATAL_FAILURE(ThenSizeEvents("data1=1920 data2=1080"));
  Escape(client);
}

TEST_F(DesktopSample, FirstFrameObserverWithoutSuccessfulConnect) {
  Client client(0, true);
  // abi: pEndPaint and pConnectCallback, BOOL is int
  auto paint   = +[](rdpContext*) -> int { return true; };
  auto connect = +[](freerdp*) -> int { return false; };
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
  ASSERT_NO_FATAL_FAILURE(GivenDesktopProcess(DesktopSize(), { "--size", "1280x800" }));
  auto& client = SessionClient();
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x40));
  ASSERT_NO_FATAL_FAILURE(ThenDesktopMode(client, 1920, 1080));
  Escape(client);
}

TEST_F(DesktopSample, FullscreenModeMovesDesktopMode) {
  ASSERT_NO_FATAL_FAILURE(GivenDesktopProcess(DesktopSize(), { "--size", "1280x800", "--mode", "1920x1080" }));
  auto& client = SessionClient();
  ASSERT_NO_FATAL_FAILURE(PressFullscreenKey(client));
  ASSERT_NO_FATAL_FAILURE(ThenDesktopMode(client, 1920, 1080));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_NO_FATAL_FAILURE(ThenDesktopMode(client, 1280, 800));
  Escape(client);
}

TEST_F(DesktopSample, CursorShape) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  PointerObserver pointer(client);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(client.Until([&] { return pointer.Red() && Pattern(client, false); }));
  SDL_Log("event POINTER width=8 height=8 argb=ffff0000 frame_has_no_red_block=1");
  Escape(client);
}

TEST_F(DesktopSample, Soname) {
  auto library = BuildRoot() / "sources/sdl-rdp/SDL3/libSDL3.so.0";
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
  ASSERT_NO_FATAL_FAILURE(GivenClipboard("hello"));
  auto& client = SessionClient();
  ASSERT_TRUE(
      client.Until([&] { return ClipboardSession().Received({ 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 0, 0 }); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=680065006c006c006f000000 text=hello");
  ASSERT_EQ(ClipboardSession().RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received({ 'h', 'e', 'l', 'l', 'o', 0 }); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=68656c6c6f00 text=hello");
  ASSERT_NO_FATAL_FAILURE(WhenAsciiClipboardOffered(client));
  Escape(client);
}

TEST_F(DesktopSample, ClipboardUnicode) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard("żółw"));
  auto&                     client = SessionClient();
  std::vector<std::uint8_t> bytes  { 0x7c, 1, 0xf3, 0, 0x42, 1, 0x77, 0, 0, 0 };
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Received(bytes); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_NO_FATAL_FAILURE(ThenLegacyClipboard(client));
  ASSERT_NO_FATAL_FAILURE(WhenClipboardEmptied(client));
  ASSERT_NO_FATAL_FAILURE(WhenUnicodeClipboardOffered(client, bytes));
  ASSERT_NO_FATAL_FAILURE(ThenClipboardCleared(client, ClipboardSession()));
  Escape(client);
}

TEST_F(DesktopSample, GraphicsPipelinePattern) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ "SDL_LOGGING=video=info" }));
  auto client = AnnouncedClient(640, 480);
  client.EnableGraphics();
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << Pattern(client, false).message();
  ASSERT_TRUE(Read("GFX advertised")) << process->Transcript();
  RecordProperty("trace", process->Transcript());
  Escape(client);
}
TEST_F(DesktopSample, InvalidCodecLogsValidNames) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), { "SDL_RDP_CODEC=avc" }));
  ASSERT_TRUE(Read(
      "ERROR: Invalid SDL_RDP_CODEC 'avc'; valid names: auto, planar, remotefx, nscodec, raw, progressive, avc420"))
      << process->Transcript();
}
TEST_F(DesktopSample, MalformedSizeIsRefused) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), { }, { "--size", "640x" }));
  ASSERT_TRUE(Read("ERROR: Invalid size '640x': expected WxH with positive sides")) << process->Transcript();
}

}
}
