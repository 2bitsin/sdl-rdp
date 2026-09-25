#pragma once
#include "client.hpp"

namespace sdl_rdp::headless_client_test::client::detail::has_cookie {
auto HasCookie(Client& client) -> bool;
}

namespace sdl_rdp::headless_client_test::client {
using detail::has_cookie::HasCookie;
}
