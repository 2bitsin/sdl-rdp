#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <freerdp/pointer.h>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_updates {
using sdl_rdp::headless_client_test::client::Client;

auto PointerUpdates(Client& client) -> rdpPointerUpdate&;
}

namespace sdl_rdp::sample_gate_test::client {
using detail::pointer_updates::PointerUpdates;
}
