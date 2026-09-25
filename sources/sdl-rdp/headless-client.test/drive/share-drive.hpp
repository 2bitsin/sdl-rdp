#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace Headless {
auto ShareDrive(Client& client, char const* path, char const* name = "share") -> void;
}
