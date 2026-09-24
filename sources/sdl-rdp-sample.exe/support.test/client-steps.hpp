#pragma once
#include <sdl-rdp-backend.so/_detail/client.hpp>
#include <filesystem>

namespace SampleGate {
auto ChangeMonitor(Headless::Client& client)                                    -> void;
auto ThenAdvanced(Headless::Client& client)                                     -> void;
auto ConnectDrive(Headless::Client& client, std::filesystem::path const& share) -> void;
}
