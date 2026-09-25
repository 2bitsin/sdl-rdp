#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace sdl_rdp::headless_client_test::drive::detail::share_drive {
using sdl_rdp::headless_client_test::client::Client;

auto ShareDrive(Client& client, char const* path, char const* name = "share") -> void;
}

namespace sdl_rdp::headless_client_test::drive {
using detail::share_drive::ShareDrive;
}
