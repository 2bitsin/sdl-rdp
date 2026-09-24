#pragma once
#include "drive-packet.hpp"

#include <cstdint>
#include <span>

namespace DriveGate {
auto Completion(unsigned device, unsigned id, unsigned status)                     -> Backend::DrivePacket;
auto ReplyTo(Backend::DrivePacket request, unsigned status)                        -> Backend::DrivePacket;
auto DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name) -> Backend::DrivePacket;
}
