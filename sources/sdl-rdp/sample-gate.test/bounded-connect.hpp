#pragma once
#include <freerdp/transport_io.h>

namespace SampleGate {
auto BoundedConnect(rdpContext* context, rdpSettings* settings, char const* hostname, int port, DWORD timeout) -> int;
}
