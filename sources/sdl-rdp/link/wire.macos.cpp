#include <sdl-rdp/link/wire.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::link::detail::wire {
using sdl_rdp::utilities::Expects;

auto SampleWire(int descriptor) -> WireSample {
  Expects(descriptor >= 0, "peer socket is open");
  return { };
}
}
