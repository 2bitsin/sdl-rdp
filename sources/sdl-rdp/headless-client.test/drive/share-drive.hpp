#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <filesystem>
#include <string>

namespace sdl_rdp::headless_client_test::drive::detail::share_drive {
using sdl_rdp::headless_client_test::client::Client;

auto ShareDrive(Client& client, std::filesystem::path const& path, std::string const& name = "share") -> void;
}

namespace sdl_rdp::headless_client_test::drive {
using detail::share_drive::ShareDrive;
}
