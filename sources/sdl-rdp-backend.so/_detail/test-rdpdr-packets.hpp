#pragma once
#include "drive-packet.hpp"

#include <cstdint>
#include <span>

namespace DriveGate {
Backend::DrivePacket Completion(unsigned device, unsigned id, unsigned status);
Backend::DrivePacket ReplyTo(Backend::DrivePacket request, unsigned status);
Backend::DrivePacket DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name);
}
