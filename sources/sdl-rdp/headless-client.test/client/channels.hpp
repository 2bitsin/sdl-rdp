#pragma once

#include <freerdp/freerdp.h>
#include <cstddef>
#include <span>

namespace Headless {
auto LoadStaticChannel(freerdp* instance, char const* name)                                   -> bool;
auto LoadDynamicChannel(freerdp* instance, char const* name)                                  -> bool;
auto SendStaticChannel(freerdp* instance, char const* name, std::span<std::byte const> bytes) -> bool;
}
