#pragma once
#include <freerdp/transport_io.h>
#include <cstdint>

namespace SampleGate {
auto BoundedConnect(rdpContext* context, rdpSettings* settings, char const* hostname, int port, std::uint32_t timeout)
    -> int;
}
