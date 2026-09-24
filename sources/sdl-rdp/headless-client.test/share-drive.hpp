#pragma once
#include "client.hpp"

namespace Headless {
auto ShareDrive(Client& client, char const* path, char const* name = "share") -> void;
}
