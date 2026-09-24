#include <sdl-rdp/bench/support.bench/session.hpp>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/video-driver.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <ranges>

namespace sdl_rdp::bench::detail::sample_video {
using support_bench::Check;
using support_bench::Measured;
using support_bench::OneSession;
using namespace std::chrono_literals;

class DefaultPresentDoesNotWaitForAcknowledgements final : public support_bench::Session<SampleGate::VideoDriver> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
BENCHMARK(Measured<DefaultPresentDoesNotWaitForAcknowledgements>)->Apply(OneSession);

auto DefaultPresentDoesNotWaitForAcknowledgements::TestBody() -> void {
  SampleGate::Client client(SampleGate::PrimaryDisplayPort(), true, 1280, 800);
  if (!Check(client.Connect(), "the client connects")) return;
  Headless::FrameObserver const observer(client);
  SDL_PumpEvents();
  if (!Check(SDL_GetWindowSurface(window) != nullptr, "the window has a surface")) return;
  bool       updated = false;
  auto const elapsed = Backend::Timed(
      [&] { updated = std::ranges::all_of(std::views::repeat(window, 10), SDL_UpdateWindowSurface); });
  Measure(elapsed);
  if (!Check(updated, "every update presents")) return;
  // Ten old 100 ms waits exceed this half-second regression budget.
  Check(elapsed < 500ms, "presents do not wait for acknowledgements");
}
}
