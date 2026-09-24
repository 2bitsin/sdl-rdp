#pragma once
#include "backend-instance.hpp"
#include "client.hpp"
#include "logs.hpp"

namespace sdl_rdp::headless_client_test::detail::await_acknowledged {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void;
}
namespace sdl_rdp::headless_client_test {
using detail::await_acknowledged::AwaitAllAcknowledged;
}
