#pragma once

#include <freerdp/freerdp.h>
#include <cstddef>
#include <span>

namespace sdl_rdp::headless_client_test::client::detail::channels {
auto LoadStaticChannel(freerdp* instance, char const* name)                                   -> bool;
auto LoadDynamicChannel(freerdp* instance, char const* name)                                  -> bool;
auto SendStaticChannel(freerdp* instance, char const* name, std::span<std::byte const> bytes) -> bool;
}

namespace sdl_rdp::headless_client_test::client {
using detail::channels::LoadDynamicChannel;
using detail::channels::LoadStaticChannel;
using detail::channels::SendStaticChannel;
}
