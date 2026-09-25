#include <sdl-rdp/drive/capabilities.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/rdpdr.h>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::drive::detail::capabilities {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
namespace {
auto Capability(DrivePacket& packet, std::uint32_t type, std::uint32_t version, DrivePacket const& body) -> void {
  constexpr std::size_t header_size = (sizeof(std::uint16_t) * 2) + sizeof(std::uint32_t);
  Expects(body.Bytes().size() <= UINT16_MAX - header_size, "capability length fits its header");
  packet.Write(Narrowed<std::uint16_t>(type));
  packet.Write(Narrowed<std::uint16_t>(header_size + body.Bytes().size()));
  packet.Write(version);
  packet.Append(body.Bytes());
}
}
auto GeneralCapability(DrivePacket& packet) -> void {
  DrivePacket body;
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint16_t{ RDPDR_VERSION_MAJOR });
  body.Write(std::uint16_t{ RDPDR_VERSION_MINOR_RDP6X });
  constexpr std::uint32_t all_irps = RDPDR_IRP_MJ_CREATE | RDPDR_IRP_MJ_CLEANUP | RDPDR_IRP_MJ_CLOSE | RDPDR_IRP_MJ_READ
                                     | RDPDR_IRP_MJ_WRITE | RDPDR_IRP_MJ_FLUSH_BUFFERS | RDPDR_IRP_MJ_SHUTDOWN
                                     | RDPDR_IRP_MJ_DEVICE_CONTROL | RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION
                                     | RDPDR_IRP_MJ_SET_VOLUME_INFORMATION | RDPDR_IRP_MJ_QUERY_INFORMATION
                                     | RDPDR_IRP_MJ_SET_INFORMATION | RDPDR_IRP_MJ_DIRECTORY_CONTROL
                                     | RDPDR_IRP_MJ_LOCK_CONTROL | RDPDR_IRP_MJ_QUERY_SECURITY
                                     | RDPDR_IRP_MJ_SET_SECURITY;
  body.Write(all_irps);
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ RDPDR_DEVICE_REMOVE_PDUS | RDPDR_CLIENT_DISPLAY_NAME_PDU | RDPDR_USER_LOGGEDON_PDU });
  body.Write(std::uint32_t{ ENABLE_ASYNCIO });
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  Capability(packet, CAP_GENERAL_TYPE, GENERAL_CAPABILITY_VERSION_02, body);
}
auto DriveCapability(DrivePacket& packet) -> void {
  Capability(packet, CAP_DRIVE_TYPE, DRIVE_CAPABILITY_VERSION_02, { });
}
}
