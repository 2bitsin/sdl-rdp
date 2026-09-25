#pragma once
#include <sdl-rdp/configuration/refresh.hpp>

namespace sdl_rdp::link::detail::wire {
using sdl_rdp::configuration::WireSample;

auto SampleWire(int descriptor) -> WireSample;
}

namespace sdl_rdp::link {
using detail::wire::SampleWire;
}
