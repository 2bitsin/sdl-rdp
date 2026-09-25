#pragma once
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::link::detail::wire_fallback {
using sdl_rdp::configuration::WireSample;
using sdl_rdp::utilities::Expects;

inline auto Unmeasured(int descriptor) -> WireSample {
  Expects(descriptor >= 0, "peer socket is open");
  return { };
}
}
