#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/video/driver.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <ranges>

namespace sdl_rdp::integration::sample_bench::detail::video {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::video::VideoDriver;
using sdl_rdp::utilities::Timed;
using namespace std::chrono_literals;

class DefaultPresentDoesNotWaitForAcknowledgements final : public Session<VideoDriver> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
BENCHMARK(Measured<DefaultPresentDoesNotWaitForAcknowledgements>)->Apply(OneSession);

auto DefaultPresentDoesNotWaitForAcknowledgements::TestBody() -> void {
  Client client(PrimaryDisplayPort(), true, 1280, 800);
  if (!Check(client.Connect(), "the client connects")) return;
  FrameObserver const observer(client);
  SDL_PumpEvents();
  if (!Check(SDL_GetWindowSurface(window.get()) != nullptr, "the window has a surface")) return;
  bool       updated = false;
  auto const elapsed = Timed(
      [&] { updated = std::ranges::all_of(std::views::repeat(window.get(), 10), SDL_UpdateWindowSurface); });
  Measure(elapsed);
  if (!Check(updated, "every update presents")) return;
  // Ten old 100 ms waits exceed this half-second regression budget.
  Check(elapsed < 500ms, "presents do not wait for acknowledgements");
}
}
