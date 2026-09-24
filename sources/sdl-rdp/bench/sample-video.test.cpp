#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/sample-gate.test/video-driver.hpp>

#include <SDL3/SDL.h>
#include <chrono>

namespace SampleGate {
TEST_F(VideoDriver, DefaultPresentDoesNotWaitForAcknowledgements) {
  auto   properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  Headless::FrameObserver const observer(client);
  SDL_PumpEvents();
  ASSERT_NE(SDL_GetWindowSurface(window), nullptr);
  auto start = Clock::now();
  for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(SDL_UpdateWindowSurface(window));
  // Ten old 100 ms waits exceed this half-second regression budget.
  EXPECT_LT(Clock::now() - start, 500ms);
}
}
