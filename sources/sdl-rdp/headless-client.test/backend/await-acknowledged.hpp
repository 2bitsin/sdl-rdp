#pragma once
#include "instance.hpp"
#include "logs.hpp"
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace sdl_rdp::headless_client_test::backend::detail::await_acknowledged {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void;
}
namespace sdl_rdp::headless_client_test::backend {
using detail::await_acknowledged::AwaitAllAcknowledged;
}
