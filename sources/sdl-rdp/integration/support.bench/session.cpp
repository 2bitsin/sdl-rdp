#include <sdl-rdp/integration/support.bench/session.hpp>

namespace sdl_rdp::integration::support_bench::detail::session {
auto OneSession(benchmark::Benchmark* bench) -> void {
  bench->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
}
}
