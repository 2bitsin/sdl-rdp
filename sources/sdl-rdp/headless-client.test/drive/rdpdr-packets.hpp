#pragma once
#include <sdl-rdp/drive/packet.hpp>

#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::drive::detail::rdpdr_packets {
using sdl_rdp::drive::DrivePacket;

auto Completion(std::uint32_t device, std::uint32_t id, std::uint32_t status)                  -> DrivePacket;
auto ReplyTo(DrivePacket request, std::uint32_t status)                                        -> DrivePacket;
auto DeviceAnnouncement(std::uint32_t type, std::uint32_t id, std::span<std::byte const> name) -> DrivePacket;
}

namespace sdl_rdp::headless_client_test::drive {
using detail::rdpdr_packets::Completion;
using detail::rdpdr_packets::DeviceAnnouncement;
using detail::rdpdr_packets::ReplyTo;
}
