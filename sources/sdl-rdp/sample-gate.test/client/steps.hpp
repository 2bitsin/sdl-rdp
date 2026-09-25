#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <filesystem>

namespace sdl_rdp::sample_gate_test::client::detail::steps {
using sdl_rdp::headless_client_test::client::Client;

auto ChangeMonitor(Client& client)                                    -> void;
auto ThenAdvanced(Client& client)                                     -> void;
auto ConnectDrive(Client& client, std::filesystem::path const& share) -> void;
}

namespace sdl_rdp::sample_gate_test::client {
using detail::steps::ChangeMonitor;
using detail::steps::ConnectDrive;
using detail::steps::ThenAdvanced;
}
