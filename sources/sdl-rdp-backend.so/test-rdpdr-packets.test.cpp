#include "_detail/test-rdpdr-packets.hpp"

#include <array>
#include <freerdp/channels/rdpdr.h>

namespace DriveGate {
auto Completion(unsigned device, unsigned id, unsigned status) -> Backend::DrivePacket {
  Backend::DrivePacket response;
  response.Put(RDPDR_CTYP_CORE, 2);
  response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  response.Put(device);
  response.Put(id);
  response.Put(status);
  return response;
}
auto ReplyTo(Backend::DrivePacket request, unsigned status) -> Backend::DrivePacket {
  auto device = request.Get(4);
  request.Skip(4);
  return Completion(device, request.Get(4), status);
}
auto DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name) -> Backend::DrivePacket {
  Backend::DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2);
  packet.Put(PAKID_CORE_DEVICELIST_ANNOUNCE, 2);
  packet.Put(1);
  packet.Put(type);
  packet.Put(id);
  packet.Append(std::array<uint8_t, 8>{ 'd', 'o', 's', 0, 0, 0, 0, 0 });
  packet.Put(name.size());
  packet.Append(name);
  return packet;
}
}
