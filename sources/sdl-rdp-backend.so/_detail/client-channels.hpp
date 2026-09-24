#pragma once

#include <freerdp/freerdp.h>
#include <span>

namespace Headless {
BOOL LoadStaticChannel(freerdp* instance, char const* name);
BOOL LoadDynamicChannel(freerdp* instance, char const* name);
bool SendStaticChannel(freerdp* instance, char const* name, std::span<BYTE const> bytes);
}
