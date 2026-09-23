#include "_detail/sample-fixture.hpp"

namespace SampleGate {
class VideoDriver : public Sample {
protected:
  void SetUp() override
  {
    Sample::SetUp();
    SDL_GetLogOutputFunction(&log_output, &log_userdata);
    SDL_SetLogOutputFunction([](void* user, int, SDL_LogPriority priority, char const* message) {
      Headless::Logs::Collect(user, priority >= SDL_LOG_PRIORITY_ERROR ? SDLRDP_LOG_ERROR : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN
                                                                                                                              : SDLRDP_LOG_INFO,
                              message);
    },
                             &logs);
    for (auto [name, value] : {
             std::pair{ SDL_HINT_VIDEO_DRIVER, "rdp"       },
             { "SDL_RDP_PORT",        "0"         },
             { "SDL_RDP_BIND",        "127.0.0.1" },
             { "SDL_RDP_CODEC",       "planar"    },
             { "SDL_RDP_WIDTH",       "1280"      },
             { "SDL_RDP_HEIGHT",      "800"       }
    }) ASSERT_TRUE(SDL_SetHint(name, value));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    auto library = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", library.c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    window = SDL_CreateWindow("desktop mode", 1280, 800, 0);
    ASSERT_NE(window, nullptr) << SDL_GetError();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  }
  void StormSizes()
  {
    auto start = Clock::now();
    for (auto [w, h] : {
             std::pair{ 1600, 900  },
             { 1920, 1080 },
             { 1280, 800  }
    }) {
      ASSERT_TRUE(SDL_SetWindowSize(window, w, h));
      ++intervening;
      std::this_thread::sleep_for(10ms);
    }
    EXPECT_LT(Clock::now() - start, 200ms);
  }
  static void Desktop(int width, int height)
  {
    const auto* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->w, width);
    EXPECT_EQ(mode->h, height);
    SDL_Event event;
    ASSERT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED,
                             SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED),
              1);
    EXPECT_EQ(event.display.data1, width);
    EXPECT_EQ(event.display.data2, height);
    EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  }
  void TearDown() override
  {
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_SetLogOutputFunction(log_output, log_userdata);
    for (const auto* hint : { SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC",
                              "SDL_RDP_WIDTH", "SDL_RDP_HEIGHT", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND" }) SDL_ResetHint(hint);
    Sample::TearDown();
  }
  SDL_Window*           window       = nullptr;
  SDL_LogOutputFunction log_output   = nullptr;
  void*                 log_userdata = nullptr;
  unsigned              intervening  = 0;
};

TEST_F(VideoDriver, ResizeStormWithLayoutEcho)
{
  ASSERT_TRUE(SDL_SetWindowSize(window, 640, 480));
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                  client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 640, 480);
  Headless::DisplayClient display(client);
  display.echo_resize        = true;
  display.finalization_delay = 20ms;
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 0));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.ready.load(); }));
  auto started = Clock::now();
  display.finalizing = [&] { if (display.desktops == 1) { StormSizes(); EXPECT_LT(Clock::now() - started, 200ms); } };
  ASSERT_TRUE(SDL_SetWindowSize(window, 1280, 800));
  std::vector<UINT32> pixels(1280uz * 800, 0);
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.desktops && client.Matches(pixels); }));
  for (unsigned i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
  EXPECT_FALSE(freerdp_shall_disconnect_context(client.instance->context));
  EXPECT_EQ(client.instance->context->gdi->width, 1280);
  EXPECT_EQ(client.instance->context->gdi->height, 800);
  EXPECT_EQ(display.desktops, 1u);
  EXPECT_EQ(display.echoes, 1u);
  EXPECT_EQ(intervening, 3u);
  EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
  RecordProperty("DesktopResize_calls", display.desktops);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  FullDesktopFrames const frames(client);
  ASSERT_TRUE(display.Layout(1280, 800));
  for (unsigned i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_WINDOW_RESIZED));
  EXPECT_EQ(frames.deliveries, 0u);
  EXPECT_EQ(display.desktops, 1u);
  RecordProperty("equal_layout_picture_resizes", 0);
}

TEST_F(VideoDriver, ExclusiveScreenChangeDoesNotResizePicture)
{
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window, &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                  client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 0));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NE(SDL_GetWindowSurface(window), nullptr);
  ASSERT_TRUE(SDL_UpdateWindowSurface(window));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return display.ready.load(); }));
  for (unsigned i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
  FullDesktopFrames const frames(client);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ASSERT_TRUE(display.Layout(1600, 900));
  ASSERT_TRUE(client.Until([&] { SDL_PumpEvents(); return SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED); }));
  for (unsigned i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
  EXPECT_EQ(frames.deliveries, 0u);
  EXPECT_EQ(display.desktops, 0u);
  EXPECT_EQ(client.instance->context->gdi->width, 1280);
  EXPECT_EQ(client.instance->context->gdi->height, 800);
  EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
  RecordProperty("DesktopResize_calls", display.desktops);
  RecordProperty("changed_layout_picture_resizes", 0);
}

TEST_F(VideoDriver, WindowResizeMovesDesktopMode)
{
  ASSERT_TRUE(SDL_SetWindowSize(window, 1920, 1080));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, FullscreenModeMovesDesktopMode)
{
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  mode.w    = 1920;
  mode.h    = 1080;
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window, &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  Desktop(1920, 1080);
  EXPECT_TRUE(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, false));
  Desktop(1280, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, InitialWindowMovesDesktopMode)
{
  SDL_DestroyWindow(window);
  window = SDL_CreateWindow("different size", 1920, 1080, 0);
  ASSERT_NE(window, nullptr);
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, DesktopFullscreenKeepsDesktopMode)
{
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
  const auto* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->w, 1280);
  EXPECT_EQ(mode->h, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, false));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
}
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
