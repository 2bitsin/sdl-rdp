#pragma once

#include <freerdp/freerdp.h>
#include <span>

namespace Headless {
auto LoadStaticChannel(freerdp* instance, char const* name)                              -> BOOL;
auto LoadDynamicChannel(freerdp* instance, char const* name)                             -> BOOL;
auto SendStaticChannel(freerdp* instance, char const* name, std::span<BYTE const> bytes) -> bool;
}
