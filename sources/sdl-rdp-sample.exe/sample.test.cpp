#include <sdl-rdp-backend.so/_detail/avc.hpp>
#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-tls.hpp>
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <cmath>

namespace SampleGate {
TEST_F(Sample, DriveDisconnectDuringCat) {
  oxbox::platform::ScratchArea share{"sample-disconnect", "sdl-rdp"};
  auto path = share.Path() / "huge.bin";
  { std::ofstream file(path); }
  fs::resize_file(path, 400 * 1024 * 1024);
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--cat", "share/huge.bin"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  {
    Client client(port, true, 640, 480);
    Headless::ShareDrive(client, share.Path().c_str());
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
    Headless::DriveObserver observer(client);
    ASSERT_TRUE(client.Until([&] {
      return std::ranges::any_of(observer.io, [](auto packet) {
        packet.Skip(12);
        return packet.Get(4) == IRP_MJ_READ;
      });
    }));
    ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  }
  ASSERT_TRUE(Read("cat failed: ")) << process->transcript;
  SDL_Log("trace DRIVE disconnected after read request: %s", line.c_str());
  Client second(port, true, 640, 480);
  Headless::ShareDrive(second, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs();
  Headless::DriveObserver observer(second);
  ASSERT_TRUE(second.Until([&] { return !observer.replies.empty() && Pattern(second, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(second));
  while (process->Line(line, Clock::now() + 1s)) {}
  EXPECT_EQ(observer.requests, 0u);
  auto failure = process->transcript.find("cat failed:");
  EXPECT_EQ(process->transcript.find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process->transcript.find("cat bytes="), std::string::npos);
  SDL_Log("trace DRIVE second client connected, frame received, cat not repeated, sample exited 0");
}

TEST_F(Sample, DriveMissingCatKeepsServing) {
  oxbox::platform::ScratchArea share{"sample-missing", "sdl-rdp"};
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--cat", "share/missing.bin"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ShareDrive(client, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(ReadInput(client, "cat failed: ")) << process->transcript;
  EXPECT_NE(line.find("Drive 'missing.bin' failed: STATUS_NO_SUCH_FILE (0xc000000f)"), std::string::npos) << line;
  // Observe a new frame after the failure, rather than inspecting an old framebuffer.
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] { return !observer.ids.empty() && Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  while (process->Line(line, Clock::now() + 1s)) {}
  auto failure = process->transcript.find("cat failed:");
  ASSERT_NE(failure, std::string::npos);
  EXPECT_EQ(process->transcript.find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process->transcript.find("cat bytes="), std::string::npos);
}

TEST_F(Sample, WholeSystem) {
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

TEST_F(Sample, RequestedSizeReturns) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
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

TEST_F(Sample, TakeoverFocus) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  Client first(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(first.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(Read("event MOUSE_ENTER "));
  Client second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs();
  for (auto expected : {"OCCLUDED", "FOCUS_LOST", "MOUSE_LEAVE", "EXPOSED", "FOCUS_GAINED", "MOUSE_ENTER"}) {
    do { ASSERT_TRUE(process->Line(line, Clock::now() + 10s)) << process->transcript; }
    while (!line.starts_with("event ") || line.starts_with("event GEOMETRY ") || line.starts_with("event CONNECTED "));
    EXPECT_TRUE(line.starts_with("event " + std::string(expected) + " ")) << line;
  }
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, AutoAvcCodecProperty) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=auto");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  client.EnableGraphics(true);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(ReadInput(client, "event CODEC_CHANGED codec=avc420")) << process->transcript;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, LiveCodec) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=remotefx");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  auto settings = client.instance->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE));
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event EXPOSED "));
  ASSERT_TRUE(line.ends_with("codec=remotefx")) << line;
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3b));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3b));
  ASSERT_TRUE(Read("event CODEC_CHANGED codec=nscodec")) << process->transcript;
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, WaitForClient) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), true));
  auto deadline = Clock::now() + 10s;
  unsigned port = 0;
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
TEST_F(Sample, DesktopIsPicture) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x480"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=1024x768"));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FullscreenFollowsScreen) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, {"SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480"});
  arguments.push_back("--fullscreen");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1024 data2=768")) << line;
  ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
  EXPECT_TRUE(line.ends_with("data1=1024 data2=768")) << line;
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1920 data2=1080")) << line;
  ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
  EXPECT_TRUE(line.ends_with("data1=1920 data2=1080")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FirstFrameObserverWithoutSuccessfulConnect) {
  Client client(0, true);
  auto paint = +[](rdpContext*) -> BOOL { return TRUE; };
  auto connect = +[](freerdp*) -> BOOL { return FALSE; };
  client.instance->context->update->EndPaint = paint;
  client.instance->PostConnect = connect;
  for (bool attempt : {false, true}) {
    {
      FirstFrameSize frame(client);
      if (attempt) EXPECT_FALSE(client.instance->PostConnect(client.instance.get()));
    }
    EXPECT_EQ(client.instance->context->update->EndPaint, paint);
    EXPECT_EQ(client.instance->PostConnect, connect);
  }
}

TEST_F(Sample, WindowResizeMovesDesktopMode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, {"SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800"});
  arguments.insert(arguments.end(), {"--size", "1280x800"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x40));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_TRUE(Read("event GEOMETRY window=1920x1080 desktop=1920x1080"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FullscreenModeMovesDesktopMode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, {"SDL_RDP_WIDTH=1280", "SDL_RDP_HEIGHT=800"});
  arguments.insert(arguments.end(), {"--size", "1280x800", "--mode", "1920x1080"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Exposed());
  auto input = client.instance->context->input;
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

class VideoDriver : public Sample {
protected:
  SDL_Window *window = nullptr;
  SDL_LogOutputFunction log_output = nullptr;
  void* log_userdata = nullptr;
  void SetUp() override {
    Sample::SetUp();
    SDL_GetLogOutputFunction(&log_output, &log_userdata);
    SDL_SetLogOutputFunction([](void* user, int, SDL_LogPriority priority, char const* message) {
      Headless::Logs::Collect(user, priority >= SDL_LOG_PRIORITY_ERROR ? SDLRDP_LOG_ERROR
        : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO, message);
    }, &logs);
    for (auto [name, value] : {std::pair{SDL_HINT_VIDEO_DRIVER, "rdp"}, {"SDL_RDP_PORT", "0"},
         {"SDL_RDP_BIND", "127.0.0.1"}, {"SDL_RDP_CODEC", "planar"},
         {"SDL_RDP_WIDTH", "1280"}, {"SDL_RDP_HEIGHT", "800"}}) ASSERT_TRUE(SDL_SetHint(name, value));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    auto library = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", library.c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    window = SDL_CreateWindow("desktop mode", 1280, 800, 0);
    ASSERT_NE(window, nullptr) << SDL_GetError();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  }
  unsigned intervening = 0;
  void StormSizes() {
    auto start = Clock::now();
    for (auto [w, h] : {std::pair{1600, 900}, {1920, 1080}, {1280, 800}}) {
      ASSERT_TRUE(SDL_SetWindowSize(window, w, h));
      ++intervening;
      std::this_thread::sleep_for(10ms);
    }
    EXPECT_LT(Clock::now() - start, 200ms);
  }
  void Desktop(int width, int height) {
    auto mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->w, width);
    EXPECT_EQ(mode->h, height);
    SDL_Event event;
    ASSERT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED,
                            SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED), 1);
    EXPECT_EQ(event.display.data1, width);
    EXPECT_EQ(event.display.data2, height);
    EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  }
  void TearDown() override {
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_SetLogOutputFunction(log_output, log_userdata);
    for (auto hint : {SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC",
         "SDL_RDP_WIDTH", "SDL_RDP_HEIGHT", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND"}) SDL_ResetHint(hint);
    Sample::TearDown();
  }
};

TEST_F(VideoDriver, ResizeStormWithLayoutEcho) {
  ASSERT_TRUE(SDL_SetWindowSize(window, 640, 480));
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 640, 480);
  Headless::DisplayClient display(client);
  display.echo_resize = true;
  display.finalization_delay = 20ms;
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 0));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.ready.load(); }));
  auto started = Clock::now();
  display.finalizing = [&] { if (display.desktops == 1) { StormSizes(); EXPECT_LT(Clock::now() - started, 200ms); } };
  ASSERT_TRUE(SDL_SetWindowSize(window, 1280, 800));
  std::vector<UINT32> pixels(1280 * 800, 0);
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.desktops && client.Matches(pixels); }));
  for (unsigned i = 0; i < 20; ++i) { ASSERT_TRUE(client.Pump(5)); SDL_PumpEvents(); }
  EXPECT_FALSE(freerdp_shall_disconnect_context(client.instance->context));
  EXPECT_EQ(client.instance->context->gdi->width, 1280);
  EXPECT_EQ(client.instance->context->gdi->height, 800);
  EXPECT_EQ(display.desktops, 1u);
  EXPECT_EQ(display.echoes, 1u);
  EXPECT_EQ(intervening, 3u);
  EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
  RecordProperty("DesktopResize_calls", display.desktops);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  FullDesktopFrames frames(client);
  ASSERT_TRUE(display.Layout(1280, 800));
  for (unsigned i = 0; i < 20; ++i) { ASSERT_TRUE(client.Pump(5)); SDL_PumpEvents(); }
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_WINDOW_RESIZED));
  EXPECT_EQ(frames.deliveries, 0u);
  EXPECT_EQ(display.desktops, 1u);
  RecordProperty("equal_layout_picture_resizes", 0);
}

TEST_F(VideoDriver, ExclusiveScreenChangeDoesNotResizePicture) {
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window, &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 0));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NE(SDL_GetWindowSurface(window), nullptr);
  ASSERT_TRUE(SDL_UpdateWindowSurface(window));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.ready.load(); }));
  for (unsigned i = 0; i < 20; ++i) { ASSERT_TRUE(client.Pump(5)); SDL_PumpEvents(); }
  FullDesktopFrames frames(client);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ASSERT_TRUE(display.Layout(1600, 900));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED); }));
  for (unsigned i = 0; i < 20; ++i) { ASSERT_TRUE(client.Pump(5)); SDL_PumpEvents(); }
  EXPECT_EQ(frames.deliveries, 0u);
  EXPECT_EQ(display.desktops, 0u);
  EXPECT_EQ(client.instance->context->gdi->width, 1280);
  EXPECT_EQ(client.instance->context->gdi->height, 800);
  EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
  RecordProperty("DesktopResize_calls", display.desktops);
  RecordProperty("changed_layout_picture_resizes", 0);
}

TEST_F(VideoDriver, WindowResizeMovesDesktopMode) {
  ASSERT_TRUE(SDL_SetWindowSize(window, 1920, 1080));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, FullscreenModeMovesDesktopMode) {
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  mode.w = 1920; mode.h = 1080;
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window, &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  Desktop(1920, 1080);
  EXPECT_TRUE(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, false));
  Desktop(1280, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, InitialWindowMovesDesktopMode) {
  SDL_DestroyWindow(window);
  window = SDL_CreateWindow("different size", 1920, 1080, 0);
  ASSERT_NE(window, nullptr);
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, DesktopFullscreenKeepsDesktopMode) {
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  auto mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->w, 1280);
  EXPECT_EQ(mode->h, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, false));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
}

TEST_F(Sample, ExplicitFullscreenBeforeConnect) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  Client client(port, true, 1280, 800);
  FirstFrameSize frame(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return frame.received; }));
  EXPECT_EQ(frame.width, 320);
  EXPECT_EQ(frame.height, 200);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ExplicitFullscreenRestoresWindow) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace explicit screen=1280x800 gdi=320x200");
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  SDL_Log("trace leave gdi=640x480 screen=1280x800");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace reenter gdi=320x200");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ExplicitFullscreenSurvivesScreenChange) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace screen=1920x1080 gdi=320x200");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  SDL_Log("trace leave gdi=640x480 screen=1920x1080");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

class RefreshMode : public Sample, public testing::WithParamInterface<const char*> {
protected:
  unsigned Count(std::string_view event) {
    Expects(!event.empty(), "event name is supplied");
    unsigned total = 0;
    for (std::size_t at = 0; (at = process->transcript.find(event, at)) != std::string::npos; at += event.size()) ++total;
    return total;
  }
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end() - 1, {"SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480"});
    std::string_view kind = GetParam();
    if (kind == "borderless") arguments.insert(arguments.end() - 1, "SDL_RDP_REFRESH=75");
    if (kind == "exclusive") {
      auto ini = certificates.Path() / "refresh.ini";
      { std::ofstream file(ini); file << "SDL_RDP_REFRESH=90\n"; }
      arguments.insert(arguments.end() - 1, "SDL_RDP_INI=" + ini.string());
    }
    arguments.push_back("--tight");
    if (kind != "windowed") arguments.push_back("--fullscreen");
    if (kind == "exclusive") arguments.insert(arguments.end(), {"--mode", "320x200"});
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void Observe(Client& client, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    auto desktop = Count("event DISPLAY_DESKTOP_MODE_CHANGED ");
    auto current = Count("event DISPLAY_CURRENT_MODE_CHANGED ");
    std::string_view kind = GetParam();
    auto rate = kind == "exclusive" ? "90" : kind == "borderless" ? "75" : "60";
    EXPECT_TRUE(process->transcript.contains(std::string("refresh=") + rate + " numerator=" + rate + " denominator=1"));
    for (unsigned i = 0; i < 6; ++i) {
      auto before = frames.ids.size();
      std::this_thread::sleep_for(i % 2 ? 20ms : 120ms);
      ASSERT_TRUE(frames.Ack());
      ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= before + 2; }));
    }
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
    EXPECT_EQ(Count("event DISPLAY_CURRENT_MODE_CHANGED "), current);
    EXPECT_EQ(Count("event DISPLAY_DESKTOP_MODE_CHANGED "), desktop);
  }
};

TEST_P(RefreshMode, AcknowledgementsPreserveDeclaredRate) {
  Expects(process == nullptr, "sample has not started");
  ASSERT_NO_FATAL_FAILURE(Start());
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
  ASSERT_TRUE(frames.Ack());
  ASSERT_NO_FATAL_FAILURE(Observe(client, frames));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Window, RefreshMode, testing::Values("windowed", "borderless", "exclusive"));

class ExclusiveFullscreen : public Sample, public testing::WithParamInterface<const char*> {
protected:
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    *std::ranges::find(arguments, std::string("SDL_RDP_CODEC=planar")) = "SDL_RDP_CODEC=" + std::string(GetParam());
    arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200", "--partial"});
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void InitialFrames(Client& client, Headless::DisplayClient& display, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
    auto initial = frames.ids.size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_TRUE(client.Until([&] { return display.ready.load() && frames.ids.size() >= initial + 2; }));
    initial = frames.ids.size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= initial + 2; }));
  }
};

TEST_P(ExclusiveFullscreen, DoesNotRepaintOnModeChanges) {
  Expects(process == nullptr, "sample has not started");
  ASSERT_NO_FATAL_FAILURE(Start());
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  FullDesktopFrames desktop(client);
  Headless::FrameObserver frames(client);
  ASSERT_NO_FATAL_FAILURE(InitialFrames(client, display, frames));
  ASSERT_GT(desktop.full, 0u);
  ASSERT_TRUE(display.Layout(1920, 1080));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "screen"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "delayed ack"));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Delivery, ExclusiveFullscreen, testing::Values("planar", "nscodec"));

TEST_F(Sample, ExplicitFullscreenKeepsDeclaredAspect) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 240; }));
  SDL_Log("trace explicit aspect=4:3 window=320x200 gdi=320x240");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AspectMapsMouse) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x350", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 192));
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=10 yrel=-35 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

static void FillSendWindow(Client& client, Headless::FrameObserver& observer) {
  Expects(observer.update != nullptr, "frame observer is installed");
  auto window = freerdp_settings_get_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge);
  ASSERT_EQ(window, 2u);
  std::size_t acknowledged = 0;
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= window; }));
  for (unsigned i = 0; i < 30; ++i) {
    ASSERT_EQ(observer.ids.size() - acknowledged, window);
    ASSERT_TRUE(observer.Ack());
    acknowledged = observer.ids.size();
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= acknowledged + window; }));
  }
}

TEST_F(Sample, SendWindowWithoutRefreshFeedback) {
  Expects(process == nullptr, "sample has not started");
  auto arguments = Arguments(certificates.Path(), false);
  arguments.push_back("--tight");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  auto mode = process->transcript.rfind("event DISPLAY_CURRENT_MODE_CHANGED ");
  ASSERT_NO_FATAL_FAILURE(FillSendWindow(client, observer));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
  EXPECT_EQ(process->transcript.rfind("event DISPLAY_CURRENT_MODE_CHANGED "), mode);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, CursorShape) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  PointerObserver pointer(client);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(client.Until([&] { return pointer.red && Pattern(client, false); }));
  SDL_Log("event POINTER width=8 height=8 argb=ffff0000 frame_has_no_red_block=1");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, Soname) {
  auto library = BuildRoot() / "sources/SDL3.so/libSDL3.so.0";
  ASSERT_TRUE(fs::is_regular_file(library));
  process = std::make_unique<Process>(std::vector<std::string>{"env", "objdump", "-p", library.string()});
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

TEST_F(Sample, ClipboardAscii) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--clip", "hello"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'h',0,'e',0,'l',0,'l',0,'o',0,0,0}); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=680065006c006c006f000000 text=hello");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'h','e','l','l','o',0}); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=68656c6c6f00 text=hello");
  ASSERT_EQ(clipboard.Offer({'w',0,'o',0,'r',0,'l',0,'d',0,0,0}), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=world"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=77006f0072006c0064000000 text=world");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ClipboardUnicode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--clip", "żółw"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  std::vector<BYTE> bytes{0x7c,1,0xf3,0,0x42,1,0x77,0,0,0};
  ASSERT_TRUE(client.Until([&] { return clipboard.Received(bytes); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'?','?','?','w',0}); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=3f3f3f7700 text=???w");
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  ASSERT_EQ(clipboard.Offer({0,0}), CHANNEL_RC_OK);
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

TEST_F(Sample, ToneAndVsync) {
  for (bool tight : {false, true}) {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
    arguments.push_back("--tone");
    if (tight) arguments.push_back("--tight");
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
    auto port = Number(std::string_view(line).substr(5));
    ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
    Client client(port, true, 640, 480);
    Headless::SoundClient audio(client);
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
    Headless::FrameObserver observer(client);
    ASSERT_TRUE(client.Until([&] {
      if (!observer.ids.empty()) observer.Ack();
      return !audio.received.empty() && Clock::now() >= audio.received.front() + 3s;
    }));
    ASSERT_EQ(audio.rate, 44100u);
    std::ranges::for_each(std::views::iota(0, 3), [&](int second) {
      auto start  = audio.received.front() + std::chrono::seconds(second);
      auto blocks = std::ranges::count_if(audio.received, [&](auto time) { return time >= start && time < start + 1s; });
      EXPECT_GE(blocks, 45) << "tight=" << tight << " second=" << second;
      SDL_Log("tone tight=%d second=%d blocks=%zu", tight, second, std::size_t(blocks));
    });
    auto [frequency, db] = Headless::ToneMeasurements(audio.samples, audio.rate);
    EXPECT_NEAR(frequency, 440, 8.8);
    EXPECT_NEAR(db, -12, 0.3);
    if (tight) EXPECT_GE(observer.ids.size(), 2u);
    RecordProperty(tight ? "tight_tone_hz" : "tone_hz", std::to_string(frequency));
    RecordProperty(tight ? "tight_tone_dbfs" : "tone_dbfs", std::to_string(db));
    ASSERT_NO_FATAL_FAILURE(Escape(client));
    process.reset();
  }
}


TEST_F(Sample, ToneAtClientRate) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
  arguments.push_back("--tone");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
  Client client(port, true, 640, 480);
  Headless::SoundClient audio(client);
  audio.rate = 48000;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] {
    if (!observer.ids.empty()) observer.Ack();
    return audio.samples.size() >= audio.rate * 2;
  }));
  ASSERT_TRUE(Read("audio device=RDP client freq=48000"));
  auto [frequency, db] = Headless::ToneMeasurements(audio.samples, audio.rate);
  EXPECT_NEAR(frequency, 440, 8.8);
  EXPECT_NEAR(db, -12, 0.3);
  RecordProperty("device_format", line);
  RecordProperty("tone_hz", std::to_string(frequency));
  RecordProperty("tone_dbfs", std::to_string(db));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

class AudioDriver : public Sample {
protected:
  SDL_LogOutputFunction previous_log = nullptr;
  void* previous_log_user = nullptr;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr, SDL_DestroyAudioStream};
  void SetUp() override {
    SDL_GetLogOutputFunction(&previous_log, &previous_log_user);
    SDL_SetLogOutputFunction([](void* user, int category, SDL_LogPriority priority, char const* text) {
      auto& self = *static_cast<AudioDriver*>(user);
      auto level = priority >= SDL_LOG_PRIORITY_ERROR ? SDLRDP_LOG_ERROR
        : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
      Headless::Logs::Collect(&self.logs, level, text);
      if (self.previous_log) self.previous_log(self.previous_log_user, category, priority, text);
    }, this);
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_PORT", "0"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BIND", "127.0.0.1"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CODEC", "planar"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    auto library = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", library.c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO)) << SDL_GetError();
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    ASSERT_TRUE(stream) << SDL_GetError();
    auto port = ListeningPort(Number(fs::read_symlink("/proc/self").string()));
    ASSERT_GT(port, 0u);
    Headless::InitializeTls(port);
  }
  void TearDown() override {
    stream.reset();
    SDL_Quit();
    SDL_SetLogOutputFunction(previous_log, previous_log_user);
    for (auto hint : {SDL_HINT_AUDIO_DRIVER, SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND",
                      "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND", "SDL_RDP_CODEC", SDL_HINT_RDP_AUDIO_LEAD}) SDL_ResetHint(hint);
  }
};
TEST_F(AudioDriver, NoClientTenSecondClock) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  EXPECT_STREQ(SDL_GetCurrentAudioDriver(), "rdp");
  std::vector<Sint16> frames(480000 * 2, 1000);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), frames.data(), frames.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  auto deadline = started + 30s;
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < deadline) SDL_Delay(5);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  // Consuming ten seconds of PCM may run one lead ahead of real time.
  // SDL may dequeue one buffer ahead; scheduling delays only make this longer.
  int buffer_frames = 0;
  SDL_AudioSpec format{};
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &format, &buffer_frames));
  EXPECT_GE(elapsed, 10.0 - 0.150 - double(buffer_frames) / format.freq);
  RecordProperty("no_client_ten_seconds_elapsed", std::to_string(elapsed));
}
TEST_F(AudioDriver, ClientReceivesOneLeadOnAttach) {
  std::vector<Sint16> pcm(48000 * 5 * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  SDL_Delay(200);
  Client client(ListeningPort(Number(fs::read_symlink("/proc/self").string())), true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return !audio.received.empty(); }));
  auto deadline = audio.received.front() + 100ms;
  while (audio.samples.size() / 2 < audio.rate * 140 / 1000 && Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
  EXPECT_GE(audio.samples.size() / 2, audio.rate * 140 / 1000);
  EXPECT_LE(audio.received.back(), deadline);
  auto first = audio.received.size();
  auto frames = audio.samples.size() / 2;
  deadline = Clock::now() + 1s;
  while (Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
  ASSERT_GT(audio.received.size(), first);
  double maximum_gap = 0;
  for (auto i = first; i < audio.received.size(); ++i)
    maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.received[i] - audio.received[i - 1]).count());
  auto block_ms = 1000.0 * (audio.samples.size() / 2 - frames) / (audio.received.size() - first) / audio.rate;
  EXPECT_LE(maximum_gap, 2 * block_ms + 10);
  auto elapsed = std::chrono::duration<double>(audio.received.back() - audio.received[first - 1]).count();
  EXPECT_NEAR(double(audio.samples.size() / 2 - frames) / audio.rate, elapsed, 0.030);
  RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
}
TEST_F(AudioDriver, StallRefillsTheLead) {
  std::vector<Sint16> pcm(48000 * 5 * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  Client client(ListeningPort(Number(fs::read_symlink("/proc/self").string())), true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() / 2 >= audio.rate / 2; }));
  ASSERT_TRUE(SDL_LockAudioStream(stream.get()));
  auto deadline = Clock::now() + 300ms;
  bool pumped = true;
  while (Clock::now() < deadline && pumped) pumped = client.Pump(1);
  auto frames = audio.samples.size() / 2;
  auto resumed = Clock::now();
  SDL_UnlockAudioStream(stream.get());
  ASSERT_TRUE(pumped);
  while (audio.samples.size() / 2 - frames < audio.rate * 150 / 1000 && Clock::now() < resumed + 100ms)
    ASSERT_TRUE(client.Pump(1));
  EXPECT_GE(audio.samples.size() / 2 - frames, audio.rate * 150 / 1000);
  EXPECT_LE(audio.received.back(), resumed + 100ms);
}
TEST_F(AudioDriver, LeadAtOrAboveLatencyFailsOpen) {
  stream.reset();
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
  for (auto lead : {"500", "501"}) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, lead));
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    EXPECT_FALSE(stream);
    EXPECT_STREQ(SDL_GetError(), "RDP audio lead must be below the audio latency window");
  }
}
TEST_F(AudioDriver, ZeroLeadKeepsRealtimeClock) {
  stream.reset();
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, "0"));
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
  stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  ASSERT_TRUE(stream) << SDL_GetError();
  std::vector<Sint16> pcm(48000 * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < started + 3s) SDL_Delay(1);
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  EXPECT_GE(Clock::now() - started, 990ms);
}
TEST_F(AudioDriver, AudioBeforeVideoSurvivesVideoQuit) {
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0);
  ASSERT_GT(port, 0);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  Client client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.ready; }));
  std::vector<Sint16> pcm(4800 * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.samples, 1234) >= 960; }));
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  EXPECT_EQ(SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0), port);
}

TEST_F(AudioDriver, AudioOnlyPlaysBlackDesktop) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  auto pid = Number(fs::read_symlink("/proc/self").string());
  auto port = ListeningPort(pid);
  ASSERT_GT(port, 0u);
  Client client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.ready; }));
  auto gdi = client.instance->context->gdi;
  std::vector<UINT32> black(std::size_t(gdi->width) * gdi->height);
  ASSERT_TRUE(client.Until([&] { return client.Matches(black); }));
  std::vector<Sint16> pcm(4800 * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.samples, 1234) >= 960; }));
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
}

}

namespace SampleGate {
TEST_F(Sample, GraphicsPipelinePattern) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_LOGGING=video=info");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << Pattern(client, false).message();
  ASSERT_TRUE(Read("GFX advertised")) << process->transcript;
  RecordProperty("trace", process->transcript);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
}

namespace SampleGate {
TEST_F(VideoDriver, DefaultPresentDoesNotWaitForAcknowledgements) {
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver observer(client);
  SDL_PumpEvents();
  ASSERT_NE(SDL_GetWindowSurface(window), nullptr);
  auto start = Clock::now();
  for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(SDL_UpdateWindowSurface(window));
  // Ten old 100 ms waits exceed this half-second regression budget.
  EXPECT_LT(Clock::now() - start, 500ms);
}

TEST_F(VideoDriver, AudioEventChangesOpenDeviceFormat) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{
    SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr), SDL_DestroyAudioStream};
  ASSERT_TRUE(stream) << SDL_GetError();
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  Headless::SoundClient audio(client);
  audio.rate = 48000;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return audio.ready; }));
  SDL_AudioSpec before{};
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &before, nullptr));
  EXPECT_EQ(before.freq, 44100);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    SDL_AudioSpec actual{};
    return SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &actual, nullptr) && actual.freq == 48000;
  }));
  SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}
}
