#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <chrono>

namespace sdl_rdp::integration::video_bench::detail::pacing {
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::utilities::Timed;
using namespace std::chrono_literals;

class NeverAcknowledgesClock final : public Session<RoundFive> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
BENCHMARK(Measured<NeverAcknowledgesClock>)->Apply(OneSession);

auto NeverAcknowledgesClock::TestBody() -> void {
  ThenNeverAcknowledges([this](auto run) {
    auto const elapsed = Timed(run);
    Measure(elapsed);
    Check(elapsed >= 200ms, "the unacknowledged frame waits its timeout");
  });
}
}
