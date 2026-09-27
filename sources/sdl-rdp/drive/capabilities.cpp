#include <sdl-rdp/drive/capabilities.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::drive::detail::capabilities {
using sdl_rdp::freerdp_facade::AllMajorFunctions;
using sdl_rdp::freerdp_facade::CapabilityType;
using sdl_rdp::freerdp_facade::CapabilityVersion;
using sdl_rdp::freerdp_facade::EnableAsyncIo;
using sdl_rdp::freerdp_facade::ExtendedPdu;
using sdl_rdp::freerdp_facade::ProtocolMajor;
using sdl_rdp::freerdp_facade::ProtocolMinorRdp6x;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
namespace {
auto Capability(DrivePacket& packet, CapabilityType type, CapabilityVersion version, DrivePacket const& body) -> void {
  constexpr std::size_t header_size = (sizeof(std::uint16_t) * 2) + sizeof(std::uint32_t);
  Expects(body.Bytes().size() <= UINT16_MAX - header_size, "capability length fits its header");
  packet.Write(type);
  packet.Write(Narrowed<std::uint16_t>(header_size + body.Bytes().size()));
  packet.Write(version);
  packet.Append(body.Bytes());
}
}
auto GeneralCapability(DrivePacket& packet) -> void {
  DrivePacket body;
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  body.Write(ProtocolMajor);
  body.Write(ProtocolMinorRdp6x);
  body.Write(AllMajorFunctions);
  body.Write(std::uint32_t{ 0 });
  body.Write(ExtendedPdu::DeviceRemove | ExtendedPdu::ClientDisplayName | ExtendedPdu::UserLoggedOn);
  body.Write(EnableAsyncIo);
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  Capability(packet, CapabilityType::General, CapabilityVersion::V2, body);
}
auto DriveCapability(DrivePacket& packet) -> void {
  Capability(packet, CapabilityType::Drive, CapabilityVersion::V2, { });
}
}
