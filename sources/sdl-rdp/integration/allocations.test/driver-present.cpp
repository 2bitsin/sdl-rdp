#include "counting-heap.hpp"
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>

namespace sdl_rdp::integration::allocations_test::detail::driver_present {
using sdl_rdp::sample_gate_test::sample::BackendLibrary;
using sdl_rdp::utilities::Expects;
namespace {
using sdl_rdp::sample_gate_test::process::InitializedSdl;
using sdl_rdp::sample_gate_test::process::Window;
constexpr int Width  = 640;
constexpr int Height = 480;
// With no peer the backend's frame pool settles at 2 buffers on the second present (measured under gdb).
constexpr std::size_t WarmPresents     = 10;
constexpr std::size_t MeasuredPresents = 100;
// Several rectangles, so storage sized per present shows as one allocation per present.
constexpr std::array Damage{ SDL_Rect{ 0, 40, Width, 32 }, SDL_Rect{ 64, 200, 64, 64 }, SDL_Rect{ 320, 400, 32, 32 } };

auto StartVideo(std::filesystem::path const& certificates) -> bool {
  auto const backend = BackendLibrary();
  for (auto [name, value] : { std::pair{ SDL_HINT_VIDEO_DRIVER, "rdp" },
                              { SDL_HINT_RDP_PORT    , "0"                  },
                              { SDL_HINT_RDP_BIND    , "127.0.0.1"          },
                              { SDL_HINT_RDP_CERT_DIR, certificates.c_str() },
                              { SDL_HINT_RDP_BACKEND , backend.c_str()      } }) {
    auto const accepted = SDL_SetHint(name, value);
    Expects(accepted, "the rdp hints are accepted");
  }
  return SDL_Init(SDL_INIT_VIDEO);
}

auto Present(SDL_Window& window) -> void {
  SDL_PumpEvents();
  ASSERT_TRUE(SDL_UpdateWindowSurfaceRects(&window, Damage.data(), static_cast<int>(Damage.size()))) << SDL_GetError();
}
auto PresentRepeatedly(SDL_Window& window, std::size_t presents) -> void {
  for (std::size_t present = 0; present < presents; ++present) ASSERT_NO_FATAL_FAILURE(Present(window));
}
auto ExpectNoneBetween(Tally const& before, Tally const& after) -> void {
  EXPECT_EQ(after.news - before.news, 0U) << "operator new calls in " << MeasuredPresents << " presents";
  EXPECT_EQ(after.heap - before.heap, 0U) << "malloc-family calls in " << MeasuredPresents << " presents";
}
}

TEST(DriverAllocations, NonePerPresentOnceWarm) {
  oxbox::platform::ScratchArea const certificates{ "driver-allocations", "sdl-rdp" };

  InitializedSdl const video{ [&] { return StartVideo(certificates.Path()); } };
  ASSERT_TRUE(video.Get()) << SDL_GetError();
  Window const window{ SDL_CreateWindow("driver allocations", Width, Height, 0) };
  ASSERT_TRUE(window) << SDL_GetError();
  ASSERT_NE(SDL_GetWindowSurface(window.get()), nullptr) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(PresentRepeatedly(*window, WarmPresents));
  auto const before = CountingHeap::Shared().Current();
  ASSERT_NO_FATAL_FAILURE(PresentRepeatedly(*window, MeasuredPresents));
  ExpectNoneBetween(before, CountingHeap::Shared().Current());
}
}
