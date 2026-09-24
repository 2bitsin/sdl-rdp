#pragma once
#include <sdl-rdp/storage/drive-packet.hpp>

#include <cstdint>
#include <span>

namespace DriveGate {
auto Completion(std::uint32_t device, std::uint32_t id, std::uint32_t status)                  -> Backend::DrivePacket;
auto ReplyTo(Backend::DrivePacket request, std::uint32_t status)                               -> Backend::DrivePacket;
auto DeviceAnnouncement(std::uint32_t type, std::uint32_t id, std::span<std::byte const> name) -> Backend::DrivePacket;
}
