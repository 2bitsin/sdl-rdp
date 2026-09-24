#include <sdl-rdp/headless-client.test/rdpdr-packets.hpp>

#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/rdpdr.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace DriveGate {
auto Completion(std::uint32_t device, std::uint32_t id, std::uint32_t status) -> Backend::DrivePacket {
  Backend::DrivePacket response;
  response.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  response.Write(std::uint16_t{ PAKID_CORE_DEVICE_IOCOMPLETION });
  response.Write(device);
  response.Write(id);
  response.Write(status);
  return response;
}
auto ReplyTo(Backend::DrivePacket request, std::uint32_t status) -> Backend::DrivePacket {
  auto device = request.Read<std::uint32_t>();
  request.Skip(4);
  return Completion(device, request.Read<std::uint32_t>(), status);
}
auto DeviceAnnouncement(std::uint32_t type, std::uint32_t id, std::span<std::byte const> name) -> Backend::DrivePacket {
  Backend::DrivePacket packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(std::uint16_t{ PAKID_CORE_DEVICELIST_ANNOUNCE });
  packet.Write(std::uint32_t{ 1 });
  packet.Write(type);
  packet.Write(id);
  constexpr std::array<char, 8> dos{ 'd', 'o', 's' };
  packet.Append(std::as_bytes(std::span(dos)));
  packet.Write(Backend::Narrowed<std::uint32_t>(name.size()));
  packet.Append(name);
  return packet;
}
}
