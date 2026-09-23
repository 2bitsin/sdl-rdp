#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/avc.hpp>
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>

namespace SampleGate {
TEST_F(Sample, WholeSystem)
{
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port ")) << "port <n>: " << process->transcript;
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_GT(port, 0u) << line;
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs() << "connect 640x480";
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event FOCUS_GAINED ")) << "FOCUS_GAINED: " << process->transcript;
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << "0x010101 background and one green 32x32 block: " << Pattern(client, false).message();
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ASSERT_TRUE(freerdp_disconnect(client.instance.get())) << "disconnect";
  ASSERT_TRUE(Read("event OCCLUDED ")) << "OCCLUDED: " << process->transcript;
  ASSERT_TRUE(Read("event FOCUS_LOST ")) << "FOCUS_LOST: " << process->transcript;
  Client second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs() << "second session connects";
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, RequestedSizeReturns)
{
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto port = Number(std::string_view(line).substr(5));
  Client first(port, true, 320, 200);
  ASSERT_TRUE(freerdp_connect(first.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=320x200"));
  ASSERT_TRUE(first.Until([&] { return Pattern(first, false); }));
  ASSERT_TRUE(freerdp_disconnect(first.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_LOST "));
  Client second(port, true, 800, 600);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=800x600"));
  ASSERT_TRUE(second.Until([&] { return Pattern(second, false); }));
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, TakeoverFocus)
{
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto port = Number(std::string_view(line).substr(5));
  Client const first(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(first.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(Read("event MOUSE_ENTER "));
  Client second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs();
  for (const auto* expected : { "OCCLUDED", "FOCUS_LOST", "MOUSE_LEAVE", "EXPOSED", "FOCUS_GAINED", "MOUSE_ENTER" }) {
    do {
      ASSERT_TRUE(process->Line(line, Clock::now() + 10s)) << process->transcript;
    } while (!line.starts_with("event ") || line.starts_with("event GEOMETRY ") || line.starts_with("event CONNECTED "));
    EXPECT_TRUE(line.starts_with("event " + std::string(expected) + " ")) << line;
  }
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, AutoAvcCodecProperty)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=auto");
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  client.EnableGraphics(true);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(ReadInput(client, "event CODEC_CHANGED codec=avc420")) << process->transcript;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, LiveCodec)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=remotefx");
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  auto* settings = client.instance->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE));
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event EXPOSED "));
  ASSERT_TRUE(line.ends_with("codec=remotefx")) << line;
  auto* input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3b));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3b));
  ASSERT_TRUE(Read("event CODEC_CHANGED codec=nscodec")) << process->transcript;
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, WaitForClient)
{
  process           = std::make_unique<Process>(Arguments(certificates.Path(), true));
  auto     deadline = Clock::now() + 10s;
  unsigned port     = 0;
  while (!(port = ListeningPort()) && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
  ASSERT_GT(port, 0u) << "sample's ephemeral listener: " << process->transcript;
  ASSERT_FALSE(Read("port ", 300ms)) << "no port line before client: " << process->transcript;
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs() << "connect to waiting sample";
  ASSERT_TRUE(Read("port ")) << "port after connection: " << process->transcript;
  ASSERT_EQ(Number(std::string_view(line).substr(5)), port) << line;
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
TEST_F(Sample, DesktopIsPicture)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--size", "640x480" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=1024x768"));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FullscreenFollowsScreen)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, { "SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480" });
  arguments.emplace_back("--fullscreen");
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client                  client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1024 data2=768")) << line;
  ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
  EXPECT_TRUE(line.ends_with("data1=1024 data2=768")) << line;
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  auto monitor = Headless::DisplayClient::Monitor(1920, 1080, 500);
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1920 data2=1080")) << line;
  ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
  EXPECT_TRUE(line.ends_with("data1=1920 data2=1080")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FirstFrameObserverWithoutSuccessfulConnect)
{
  Client client(0, true);
  auto paint   = +[](rdpContext*) -> BOOL { return TRUE; };
  auto connect = +[](freerdp*) -> BOOL { return FALSE; };
  client.instance->context->update->EndPaint = paint;
  client.instance->PostConnect               = connect;
  for (bool const attempt : { false, true }) {
    {
      FirstFrameSize const frame(client);
      if (attempt) EXPECT_FALSE(client.instance->PostConnect(client.instance.get()));
    }
    EXPECT_EQ(client.instance->context->update->EndPaint, paint);
    EXPECT_EQ(client.instance->PostConnect, connect);
  }
}

TEST_F(Sample, WindowResizeMovesDesktopMode)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, { "SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800" });
  arguments.insert(arguments.end(), { "--size", "1280x800" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x40));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_TRUE(Read("event GEOMETRY window=1920x1080 desktop=1920x1080"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FullscreenModeMovesDesktopMode)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, { "SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800" });
  arguments.insert(arguments.end(), { "--size", "1280x800", "--mode", "1920x1080" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Exposed());
  auto* input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_TRUE(Read("event GEOMETRY window=1920x1080 desktop=1920x1080"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1280 height=800"));
  ASSERT_TRUE(Read("event GEOMETRY window=1280x800 desktop=1280x800"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1280 && gdi->height == 800; }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, CursorShape)
{
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  PointerObserver pointer(client);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(client.Until([&] { return pointer.red && Pattern(client, false); }));
  SDL_Log("event POINTER width=8 height=8 argb=ffff0000 frame_has_no_red_block=1");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, Soname)
{
  auto library = BuildRoot() / "sources/SDL3.so/libSDL3.so.0";
  ASSERT_TRUE(fs::is_regular_file(library));
  process    = std::make_unique<Process>(std::vector<std::string>{ "env", "objdump", "-p", library.string() });
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

TEST_F(Sample, ClipboardAscii)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--clip", "hello" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client                    client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 0, 0 }); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=680065006c006c006f000000 text=hello");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'h', 'e', 'l', 'l', 'o', 0 }); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=68656c6c6f00 text=hello");
  ASSERT_EQ(clipboard.Offer({ 'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0, 0, 0 }), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=world"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=77006f0072006c0064000000 text=world");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ClipboardUnicode)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--clip", "żółw" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client                    client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  std::vector<BYTE> bytes{ 0x7c, 1, 0xf3, 0, 0x42, 1, 0x77, 0, 0, 0 };
  ASSERT_TRUE(client.Until([&] { return clipboard.Received(bytes); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ '?', '?', '?', 'w', 0 }); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=3f3f3f7700 text=???w");
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  ASSERT_EQ(clipboard.Offer({ 0, 0 }), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
  ASSERT_EQ(clipboard.Offer(bytes), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 2; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_EQ(clipboard.Offer({}, false), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.accepted.load() == 4; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
  SDL_Log("trace CLIPBOARD client formats=8 text-cleared=1");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, GraphicsPipelinePattern)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_LOGGING=video=info");
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << Pattern(client, false).message();
  ASSERT_TRUE(Read("GFX advertised")) << process->transcript;
  RecordProperty("trace", process->transcript);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
TEST_F(Sample, InvalidCodecLogsValidNames) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=avc");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("ERROR: Invalid SDL_RDP_CODEC 'avc'; valid names: auto, planar, remotefx, nscodec, raw, progressive, avc420"))
    << process->transcript;
}

}
