#pragma once
#include <freerdp/transport_io.h>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::client::detail::bounded_connect {
auto BoundedConnect(rdpContext* context, rdpSettings* settings, char const* hostname, int port, std::uint32_t timeout)
    -> int;
}

namespace sdl_rdp::sample_gate_test::client {
using detail::bounded_connect::BoundedConnect;
}
