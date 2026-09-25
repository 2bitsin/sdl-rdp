#include <sdl-rdp/link/wire.hpp>

#include <sdl-rdp/link/wire-fallback.hpp>

namespace sdl_rdp::link::detail::wire {
using sdl_rdp::link::detail::wire_fallback::Unmeasured;
auto SampleWire(int descriptor) -> WireSample {
  return Unmeasured(descriptor);
}
}
