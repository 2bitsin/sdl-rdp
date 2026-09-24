#include <sdl-rdp/sample-gate.test/video-driver.hpp>

#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

#include <utility>

namespace SampleGate {
auto VideoDriver::ThenDesktopPicture(Client const& client, Headless::DisplayClient& display) -> void {
  EXPECT_EQ(client.Instance()->context->gdi->width, 1280);
  EXPECT_EQ(client.Instance()->context->gdi->height, 800);
  EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
  RecordProperty("DesktopResize_calls", display.Observed().desktops);
}
auto VideoDriver::ThenDesktopEvent(int width, int height) -> void {
  SDL_Event event;
  ASSERT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED,
                           SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED),
            1);
  EXPECT_EQ(event.display.data1, width);
  EXPECT_EQ(event.display.data2, height);
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
}
auto VideoDriver::GivenFullscreen() -> void {
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window, &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, true));
}
auto VideoDriver::GivenVideoHints() -> void {
  ASSERT_TRUE(
      SetLoopbackHints(certificates.Path(),
                       { { "SDL_RDP_CODEC", "planar" }, { "SDL_RDP_WIDTH", "1280" }, { "SDL_RDP_HEIGHT", "800" } }));
}
auto VideoDriver::SetUp() -> void {
  ASSERT_NO_FATAL_FAILURE(Sample::SetUp());
  SDL_GetLogOutputFunction(&log_output, &log_userdata);
  SDL_SetLogOutputFunction(
      [](void* user, int, SDL_LogPriority priority, char const* message) {
        Headless::Logs::Collect(user,
                                priority >= SDL_LOG_PRIORITY_ERROR ? SDLRDP_LOG_ERROR
                                : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN
                                                                    : SDLRDP_LOG_INFO,
                                message);
      },
      &logs);
  ASSERT_NO_FATAL_FAILURE(GivenVideoHints());
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
  window = SDL_CreateWindow("desktop mode", 1280, 800, 0);
  ASSERT_NE(window, nullptr) << SDL_GetError();
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}
auto VideoDriver::StormSizes() -> void {
  for (auto [w, h] : { std::pair{ 1600, 900 }, { 1920, 1080 }, { 1280, 800 } }) {
    ASSERT_TRUE(SDL_SetWindowSize(window, w, h));
  }
}
auto VideoDriver::Desktop(int width, int height) -> void {
  auto const* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->w, width);
  EXPECT_EQ(mode->h, height);
  ThenDesktopEvent(width, height);
}
auto VideoDriver::TearDown() -> void {
  SDL_DestroyWindow(window);
  SDL_Quit();
  SDL_SetLogOutputFunction(log_output, log_userdata);
  for (auto const* hint : { SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC", "SDL_RDP_WIDTH",
                            "SDL_RDP_HEIGHT", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND" })
    SDL_ResetHint(hint);
  Sample::TearDown();
}
auto VideoDriver::ThenResizeStorm(Client& client, Headless::DisplayClient& display) -> void {
  EXPECT_FALSE(freerdp_shall_disconnect_context(client.Instance()->context));
  EXPECT_EQ(display.Observed().desktops, 1u);
  EXPECT_EQ(display.Observed().echoes, 1u);
  ThenDesktopPicture(client, display);
}
auto VideoDriver::ThenExclusivePicture(Client& client, Headless::DisplayClient& display,
                                       FullDesktopFrames const& frames) -> void {
  EXPECT_EQ(frames.Deliveries(), 0u);
  EXPECT_EQ(display.Observed().desktops, 0u);
  ThenDesktopPicture(client, display);
  RecordProperty("changed_layout_picture_resizes", 0);
}
}
