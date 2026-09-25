#pragma once
#include "client.hpp"

#include <freerdp/freerdp.h>

namespace sdl_rdp::headless_client_test::client::detail::handles {
auto ClientHandle(Client& client)  -> freerdp&;
auto ClientContext(Client& client) -> rdpContext&;
auto ClientUpdates(Client& client) -> rdpUpdate&;
}

namespace sdl_rdp::headless_client_test::client {
using detail::handles::ClientContext;
using detail::handles::ClientHandle;
using detail::handles::ClientUpdates;
}
