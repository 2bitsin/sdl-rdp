#include <sdl-rdp/headless-client.test/display-client.hpp>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <sdl-rdp/sample-gate.test/full-desktop-frames.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>
#include <sdl-rdp/sample-gate.test/video-driver.hpp>

#include <SDL3/SDL.h>
#include <memory>
#include <utility>
#include <vector>

namespace SampleGate {

namespace {
auto ThenUnchangedPicture(Headless::DisplayClient& display, FullDesktopFrames const& frames) -> void {
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_WINDOW_RESIZED));
  EXPECT_EQ(frames.Deliveries(), 0u);
  EXPECT_EQ(display.Observed().desktops, 1u);
  testing::Test::RecordProperty("equal_layout_picture_resizes", 0);
}
auto PumpDesktop(Client& client) -> void {
  for (unsigned i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
}
}
namespace {
auto ThenEqualLayout(Client& client, Headless::DisplayClient& display) -> void {
  FullDesktopFrames const frames(client);
  ASSERT_TRUE(display.Layout(1280, 800));
  PumpDesktop(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenUnchangedPicture(display, frames);
}
}
namespace {
auto ThenAudioDeviceChanges(Client& client, SDL_AudioStream* stream) -> void {
  SDL_AudioSpec before{ };
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &before, nullptr));
  EXPECT_EQ(before.freq, 44100);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    SDL_AudioSpec actual{ };
    return SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &actual, nullptr) && actual.freq == 48000;
  }));
}
}
namespace {
auto AwaitResizedPicture(Client& client, Headless::DisplayClient& display) -> void {
  Expects(client.Instance() != nullptr, "resized client exists");
  std::vector<UINT32> pixels(1280uz * 800, 0);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return display.Observed().desktops && client.Matches(pixels);
  }));
}
auto PresentDesktop(Client& client, SDL_Window* window) -> void {
  auto* surface = SDL_GetWindowSurface(window);
  ASSERT_NE(surface, nullptr);
  ASSERT_TRUE(SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0x12, 0x34, 0x56)));
  ASSERT_TRUE(SDL_UpdateWindowSurface(window));
  std::vector<UINT32> pixels(1280uz * 800, 0x00123456);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
}
auto ConnectDesktop(Client& client, Headless::Logs& logs) -> void {
  Expects(client.Instance() != nullptr, "desktop client exists");
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, 0));
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return Headless::DisplayClient::Ready();
  }));
}
}
TEST_F(VideoDriver, ResizeStormWithLayoutEcho) {
  ASSERT_TRUE(SDL_SetWindowSize(window, 640, 480));
  auto                    properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 640, 480);
  Headless::DisplayClient display(client);
  display.Observed().echo_resize = true;
  ConnectDesktop(client, logs);
  if (::testing::Test::HasFatalFailure()) return;
  display.Observed().finalizing = [&] {
    if (display.Observed().desktops == 1) {
      StormSizes();
    }
  };
  ASSERT_TRUE(SDL_SetWindowSize(window, 1280, 800));
  AwaitResizedPicture(client, display);
  if (::testing::Test::HasFatalFailure()) return;
  PumpDesktop(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenResizeStorm(client, display);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ThenEqualLayout(client, display);
}

TEST_F(VideoDriver, ExclusiveScreenChangeDoesNotResizePicture) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen());
  auto                    properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  Headless::DisplayClient display(client);
  ConnectDesktop(client, logs);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_NO_FATAL_FAILURE(PresentDesktop(client, window));
  FullDesktopFrames const frames(client);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ASSERT_TRUE(display.Layout(1600, 900));
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED);
  }));
  PumpDesktop(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenExclusivePicture(client, display, frames);
}

TEST_F(VideoDriver, WindowResizeMovesDesktopMode) {
  ASSERT_TRUE(SDL_SetWindowSize(window, 1920, 1080));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, FullscreenModeMovesDesktopMode) {
  auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  mode.w = 1920;
  mode.h = 1080;
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
  auto const* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->w, 1280);
  EXPECT_EQ(mode->h, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window, false));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
}

TEST_F(VideoDriver, AudioEventChangesOpenDeviceFormat) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));
  SDL_AudioSpec const                                                       spec  { SDL_AUDIO_S16, 2, 44100 };
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> const stream{
    SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr), SDL_DestroyAudioStream
  };
  ASSERT_TRUE(stream) << SDL_GetError();
  auto                  properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  Headless::SoundClient audio(client);
  audio.CaptureState().rate = 48000;
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().ready; }));
  ThenAudioDeviceChanges(client, stream.get());
  if (::testing::Test::HasFatalFailure()) return;
  SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}
}
