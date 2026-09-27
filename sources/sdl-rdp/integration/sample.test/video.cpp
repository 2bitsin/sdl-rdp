#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/sample-gate.test/frame/full-desktop.hpp>
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>
#include <sdl-rdp/sample-gate.test/video/driver.hpp>

#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace sdl_rdp::integration::sample_test::detail::video {
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::DisplayClient;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::SettingsOf;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::sample_gate_test::frame::FullDesktopFrames;
using sdl_rdp::sample_gate_test::process::AudioStream;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::video::VideoDriver;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Extent;

namespace {
auto ThenUnchangedPicture(DisplayClient& display, FullDesktopFrames const& frames) -> void {
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_WINDOW_RESIZED));
  EXPECT_EQ(frames.Deliveries(), 0u);
  EXPECT_EQ(display.Observed().desktops, 1u);
  testing::Test::RecordProperty("equal_layout_picture_resizes", 0);
}
auto PumpDesktop(Client& client) -> void {
  for (std::size_t i = 0; i < 20; ++i) {
    ASSERT_TRUE(client.Pump(5));
    SDL_PumpEvents();
  }
}
auto ThenEqualLayout(Client& client, DisplayClient& display) -> void {
  FullDesktopFrames const frames(client);
  ASSERT_TRUE(display.Layout(1280, 800));
  ASSERT_NO_FATAL_FAILURE(PumpDesktop(client));
  ThenUnchangedPicture(display, frames);
}
auto ThenAudioDeviceChanges(Client& client, SDL_AudioStream& stream) -> void {
  SDL_AudioSpec before{ };
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(&stream), &before, nullptr));
  EXPECT_EQ(before.freq, 44100);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    SDL_AudioSpec actual{ };
    return SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(&stream), &actual, nullptr) && actual.freq == 48000;
  }));
}
auto AwaitResizedPicture(Client& client, DisplayClient& display) -> void {
  Expects(client.Instance() != nullptr, "resized client exists");
  Pixels pixels(1280uz * 800, 0);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return display.Observed().desktops && client.Matches(pixels);
  }));
}
auto PresentDesktop(Client& client, SDL_Window& window) -> void {
  auto* surface = SDL_GetWindowSurface(&window);
  ASSERT_NE(surface, nullptr);
  ASSERT_TRUE(SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0x12, 0x34, 0x56)));
  ASSERT_TRUE(SDL_UpdateWindowSurface(&window));
  Pixels const pixels(1280uz * 800, 0x00123456);
  ASSERT_TRUE(UntilMatches(client, pixels));
}
auto ConnectDesktop(Client& client, Logs& logs) -> void {
  Expects(client.Instance() != nullptr, "desktop client exists");
  SettingsOf(client).Set(NumberKey::FrameAcknowledge, 0U);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return DisplayClient::Of(client, &DisplayClient::Ready);
  }));
}
}
TEST_F(VideoDriver, ResizeStormWithLayoutEcho) {
  ASSERT_TRUE(SDL_SetWindowSize(window.get(), 640, 480));
  Client        client(PrimaryDisplayPort(), true, 640, 480);
  DisplayClient display(client);
  display.Observed().echo_resize = true;
  ASSERT_NO_FATAL_FAILURE(ConnectDesktop(client, logs));
  display.Observed().finalizing = [&] {
    if (display.Observed().desktops == 1) {
      ASSERT_NO_FATAL_FAILURE(StormSizes());
    }
  };
  ASSERT_TRUE(SDL_SetWindowSize(window.get(), 1280, 800));
  ASSERT_NO_FATAL_FAILURE(AwaitResizedPicture(client, display));
  ASSERT_NO_FATAL_FAILURE(PumpDesktop(client));
  ThenResizeStorm(client, display);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ThenEqualLayout(client, display);
}

TEST_F(VideoDriver, ExclusiveScreenChangeDoesNotResizePicture) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen());
  Client        client(PrimaryDisplayPort(), true, 1280, 800);
  DisplayClient display(client);
  ASSERT_NO_FATAL_FAILURE(ConnectDesktop(client, logs));
  ASSERT_NO_FATAL_FAILURE(PresentDesktop(client, *window));
  FullDesktopFrames const frames(client);
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  ASSERT_TRUE(display.Layout(1600, 900));
  ASSERT_TRUE(client.Until([&] {
    SDL_PumpEvents();
    return SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED);
  }));
  ASSERT_NO_FATAL_FAILURE(PumpDesktop(client));
  ThenExclusivePicture(client, display, frames);
}

TEST_F(VideoDriver, WindowResizeMovesDesktopMode) {
  ASSERT_TRUE(SDL_SetWindowSize(window.get(), 1920, 1080));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, FullscreenModeMovesDesktopMode) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen(Extent{ .width = 1920, .height = 1080 }));
  ASSERT_NO_FATAL_FAILURE(Desktop(1920, 1080));
  EXPECT_TRUE(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window.get(), false));
  ASSERT_NO_FATAL_FAILURE(Desktop(1280, 800));
  ASSERT_TRUE(SDL_SetWindowFullscreen(window.get(), true));
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, InitialWindowMovesDesktopMode) {
  window.reset();
  window.reset(SDL_CreateWindow("different size", 1920, 1080, 0));
  ASSERT_NE(window, nullptr);
  Desktop(1920, 1080);
}

TEST_F(VideoDriver, DesktopFullscreenKeepsDesktopMode) {
  ASSERT_TRUE(SDL_SetWindowFullscreen(window.get(), true));
  auto const* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->w, 1280);
  EXPECT_EQ(mode->h, 800);
  ASSERT_TRUE(SDL_SetWindowFullscreen(window.get(), false));
  EXPECT_FALSE(SDL_HasEvent(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED));
}

TEST_F(VideoDriver, AudioEventChangesOpenDeviceFormat) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));
  SDL_AudioSpec const spec  { SDL_AUDIO_S16, 2, 44100                                                               };
  AudioStream const   stream{ SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr) };
  ASSERT_TRUE(stream) << SDL_GetError();
  Client      client(PrimaryDisplayPort(), true, 1280, 800);
  SoundClient audio(client);
  audio.CaptureState().rate = 48000;
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().ready; }));
  ASSERT_NO_FATAL_FAILURE(ThenAudioDeviceChanges(client, *stream));
}
}
