#pragma once
#include <freerdp/transport_io.h>

namespace SampleGate {
int BoundedConnect(rdpContext* context, rdpSettings* settings, char const* hostname, int port, DWORD timeout);
}
