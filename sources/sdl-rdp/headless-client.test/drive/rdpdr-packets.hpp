#pragma once
#include <sdl-rdp/drive/packet.hpp>

#include <cstdint>
#include <span>

namespace DriveGate {
auto Completion(std::uint32_t device, std::uint32_t id, std::uint32_t status) -> sdl_rdp::drive::DrivePacket;
auto ReplyTo(sdl_rdp::drive::DrivePacket request, std::uint32_t status)       -> sdl_rdp::drive::DrivePacket;
auto DeviceAnnouncement(std::uint32_t type, std::uint32_t id, std::span<std::byte const> name)
    -> sdl_rdp::drive::DrivePacket;
}
