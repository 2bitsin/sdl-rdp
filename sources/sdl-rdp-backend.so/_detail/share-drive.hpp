#pragma once
#include "client.hpp"

namespace Headless {
void ShareDrive(Client& client, char const* path, char const* name = "share");
}
