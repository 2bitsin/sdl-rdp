#pragma once
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstdint>

namespace sdl_rdp::drive::detail::information {
using sdl_rdp::freerdp_facade::InformationClass;

struct BasicInformation {
  bool         directory{ };
  std::int64_t modified { };
};
auto InformationRequest(InformationClass type, DrivePacket const& body) -> DrivePacket;
auto Information(DrivePacket response)                                  -> DrivePacket;
auto Basic(DrivePacket basic)                                           -> BasicInformation;
auto EndOfFile(DrivePacket standard)                                    -> std::uint64_t;
auto UnixSeconds(std::uint64_t filetime)                                -> std::int64_t;
}

namespace sdl_rdp::drive {
using detail::information::Basic;
using detail::information::BasicInformation;
using detail::information::EndOfFile;
using detail::information::Information;
using detail::information::InformationRequest;
using detail::information::UnixSeconds;
}
