#pragma once

#include <freerdp/freerdp.h>
#include <cstddef>
#include <span>
#include <string>

namespace sdl_rdp::headless_client_test::client::detail::channels {
auto LoadStaticChannel(freerdp& instance, std::string const& name)                                   -> bool;
auto LoadDynamicChannel(freerdp& instance, std::string const& name)                                  -> bool;
auto SendStaticChannel(freerdp& instance, std::string const& name, std::span<std::byte const> bytes) -> bool;
}

namespace sdl_rdp::headless_client_test::client {
using detail::channels::LoadDynamicChannel;
using detail::channels::LoadStaticChannel;
using detail::channels::SendStaticChannel;
}
