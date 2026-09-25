#pragma once
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/sample-gate.test/frame/full-desktop.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <SDL3/SDL.h>

namespace SampleGate {
class VideoDriver : public Sample {
protected:
  auto        ThenDesktopPicture(Client const& client, Headless::DisplayClient& display) -> void;
  static auto ThenDesktopEvent(int width, int height)                                    -> void;
  auto        GivenFullscreen()                                                          -> void;
  auto        GivenVideoHints()                                                          -> void;
  auto        SetUp()                                                                    -> void override;
  auto        StormSizes()                                                               -> void;
  static auto Desktop(int width, int height)                                             -> void;
  auto        TearDown()                                                                 -> void override;
  auto        ThenResizeStorm(Client& client, Headless::DisplayClient& display)          -> void;
  auto ThenExclusivePicture(Client& client, Headless::DisplayClient& display, FullDesktopFrames const& frames) -> void;
  SDL_Window*           window       = nullptr;
  SDL_LogOutputFunction log_output   = nullptr;
  void*                 log_userdata = nullptr;
};
}
