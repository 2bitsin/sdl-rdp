#include <sdl-rdp/integration/support.bench/session.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::integration::support_bench::detail::session {
using sdl_rdp::utilities::Expects;

auto OneSession(benchmark::Benchmark* bench) -> void {
  Expects(bench != nullptr, "the registration is supplied");
  bench->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
}
}
