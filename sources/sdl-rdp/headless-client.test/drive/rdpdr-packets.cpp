#include <sdl-rdp/headless-client.test/drive/rdpdr-packets.hpp>

#include <freerdp/channels/rdpdr.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::drive::detail::rdpdr_packets {
auto Completion(std::uint32_t device, std::uint32_t id, std::uint32_t status) -> DrivePacket {
  DrivePacket response;
  response.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  response.Write(std::uint16_t{ PAKID_CORE_DEVICE_IOCOMPLETION });
  response.Write(device);
  response.Write(id);
  response.Write(status);
  return response;
}
auto ReplyTo(DrivePacket request, std::uint32_t status) -> DrivePacket {
  auto device = request.Read<std::uint32_t>();
  request.Skip(4);
  return Completion(device, request.Read<std::uint32_t>(), status);
}
auto DeviceAnnouncement(std::uint32_t type, std::uint32_t id, std::span<std::byte const> name) -> DrivePacket {
  DrivePacket packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(std::uint16_t{ PAKID_CORE_DEVICELIST_ANNOUNCE });
  packet.Write(std::uint32_t{ 1 });
  packet.Write(type);
  packet.Write(id);
  constexpr std::array<char, 8> dos{ 'd', 'o', 's' };
  packet.Append(std::as_bytes(std::span(dos)));
  packet.AppendCounted(name);
  return packet;
}
}
