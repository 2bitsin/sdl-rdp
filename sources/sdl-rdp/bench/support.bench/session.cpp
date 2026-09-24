#include <sdl-rdp/bench/support.bench/session.hpp>

namespace sdl_rdp::bench::support_bench::detail::session {
auto OneSession(benchmark::Benchmark* bench) -> void {
  bench->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
}
}
