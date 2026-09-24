#include <sdl-rdp/headless-client.test/rdpdr-packets.hpp>

#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/rdpdr.h>
#include <array>
#include <span>

namespace DriveGate {
auto Completion(unsigned device, unsigned id, unsigned status) -> Backend::DrivePacket {
  Backend::DrivePacket response;
  response.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  response.Write(std::uint16_t{ PAKID_CORE_DEVICE_IOCOMPLETION });
  response.Write(std::uint32_t{ device });
  response.Write(std::uint32_t{ id });
  response.Write(std::uint32_t{ status });
  return response;
}
auto ReplyTo(Backend::DrivePacket request, unsigned status) -> Backend::DrivePacket {
  auto device = request.Read<uint32_t>();
  request.Skip(4);
  return Completion(device, request.Read<uint32_t>(), status);
}
auto DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name) -> Backend::DrivePacket {
  Backend::DrivePacket packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(std::uint16_t{ PAKID_CORE_DEVICELIST_ANNOUNCE });
  packet.Write(std::uint32_t{ 1 });
  packet.Write(std::uint32_t{ type });
  packet.Write(std::uint32_t{ id });
  constexpr std::array<char, 8> dos{ 'd', 'o', 's' };
  packet.Append(std::as_bytes(std::span(dos)));
  packet.Write(Backend::Narrowed<std::uint32_t>(name.size()));
  packet.Append(std::as_bytes(name));
  return packet;
}
}
