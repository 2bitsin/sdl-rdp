#pragma once
#include "instance.hpp"
#include "logs.hpp"
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <chrono>
#include <string_view>

namespace sdl_rdp::headless_client_test::backend::detail::waits {
using sdl_rdp::headless_client_test::client::Client;

auto AwaitAllAcknowledged(Client& client, BackendInstance const& backend, Logs& logs) -> void;
auto ConnectWithCookie(Client& client, Logs& logs)                                    -> void;
auto UntilCookie(Client& client)                                                      -> bool;
auto UntilLogged(Client& client, Logs& logs, std::string_view text,
                 std::chrono::milliseconds timeout = std::chrono::seconds(10)) -> bool;
}

namespace sdl_rdp::headless_client_test::backend {
using detail::waits::AwaitAllAcknowledged;
using detail::waits::ConnectWithCookie;
using detail::waits::UntilCookie;
using detail::waits::UntilLogged;
}
