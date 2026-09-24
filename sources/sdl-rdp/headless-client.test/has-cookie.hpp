#pragma once
#include "client.hpp"

namespace BackendGate {
auto HasCookie(Headless::Client const& client) -> bool;
}
