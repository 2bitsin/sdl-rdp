#pragma once
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/utilities/socket.hpp>

namespace sdl_rdp::link::detail::wire {
using sdl_rdp::configuration::WireSample;
using sdl_rdp::utilities::NativeSocket;

auto SampleWire(NativeSocket socket) -> WireSample;
}

namespace sdl_rdp::link {
using detail::wire::SampleWire;
}
