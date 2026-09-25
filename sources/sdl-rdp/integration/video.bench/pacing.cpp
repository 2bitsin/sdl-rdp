#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <chrono>

namespace sdl_rdp::integration::video_bench::detail::pacing {
using support_bench::Check;
using support_bench::Measured;
using support_bench::OneSession;
using namespace std::chrono_literals;

class NeverAcknowledgesClock final : public support_bench::Session<BackendGate::RoundFive> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
BENCHMARK(Measured<NeverAcknowledgesClock>)->Apply(OneSession);

auto NeverAcknowledgesClock::TestBody() -> void {
  ThenNeverAcknowledges([this](auto run) {
    auto const elapsed = Backend::Timed(run);
    Measure(elapsed);
    Check(elapsed >= 200ms, "the unacknowledged frame waits its timeout");
  });
}
}
