#include <sdl-rdp/sample-gate.test/video/driver.hpp>

#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <utility>

namespace sdl_rdp::sample_gate_test::video::detail::driver {
using sdl_rdp::sample_gate_test::sample::SetLoopbackHints;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Narrowed;

auto VideoDriver::ThenDesktopPicture(Client& client, DisplayClient& display) -> void {
  EXPECT_EQ(client.DesktopSize(), (Extent{ .width = 1280, .height = 800 }));
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
auto VideoDriver::GivenFullscreen(std::optional<Extent> size) -> void {
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  if (size) {
    mode.w = Narrowed<int>(size->width);
    mode.h = Narrowed<int>(size->height);
  }
  ASSERT_TRUE(SDL_SetWindowFullscreenMode(window.get(), &mode));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window.get(), true));
}
auto VideoDriver::SetUp() -> void {
  ASSERT_NO_FATAL_FAILURE(Sample::SetUp());
  captured.emplace(logs);
  sdl.emplace([this] {
    return SetLoopbackHints(certificates.Path(), { { .name = "SDL_RDP_CODEC" , .value = "planar" },
                                                   { .name = "SDL_RDP_WIDTH" , .value = "1280"   },
                                                   { .name = "SDL_RDP_HEIGHT", .value = "800"    } })
           && SDL_Init(SDL_INIT_VIDEO);
  });
  ASSERT_TRUE(sdl->Get()) << SDL_GetError();
  window.reset(SDL_CreateWindow("desktop mode", 1280, 800, 0));
  ASSERT_NE(window, nullptr) << SDL_GetError();
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}
auto VideoDriver::StormSizes() -> void {
  for (auto [w, h] : { std::pair{ 1600, 900 }, { 1920, 1080 }, { 1280, 800 } }) {
    ASSERT_TRUE(SDL_SetWindowSize(window.get(), w, h));
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
  window.reset();
  sdl.reset();
  captured.reset();
  Sample::TearDown();
}
auto VideoDriver::ThenResizeStorm(Client& client, DisplayClient& display) -> void {
  EXPECT_FALSE(freerdp_shall_disconnect_context(client.Instance()->context));
  EXPECT_EQ(display.Observed().desktops, 1u);
  EXPECT_EQ(display.Observed().echoes, 1u);
  ThenDesktopPicture(client, display);
}
auto VideoDriver::ThenExclusivePicture(Client& client, DisplayClient& display, FullDesktopFrames const& frames)
    -> void {
  EXPECT_EQ(frames.Deliveries(), 0u);
  EXPECT_EQ(display.Observed().desktops, 0u);
  ThenDesktopPicture(client, display);
  RecordProperty("changed_layout_picture_resizes", 0);
}
}
