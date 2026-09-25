#pragma once
#include <sdl-rdp/drive/packet.hpp>

namespace sdl_rdp::drive::detail::capabilities {
auto GeneralCapability(DrivePacket& packet) -> void;
auto DriveCapability(DrivePacket& packet)   -> void;
}
namespace sdl_rdp::drive {
using detail::capabilities::GeneralCapability;
using detail::capabilities::DriveCapability;
}
