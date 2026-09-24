#pragma once
#include "backend-instance.hpp"
#include "client.hpp"
#include "logs.hpp"

namespace BackendGate {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void;
}
