#pragma once
#include <cstdint>

namespace sdl_rdp::freerdp_facade::detail::rdpdr {
// The file-system constants the rdpdr channel carries (MS-RDPEFS 2.2.1.4, MS-FSCC 2.4 and 2.6, MS-SMB2 2.2.13.1).
enum class IrpMajor : std::uint32_t {
  Create           = 0x00,
  Close            = 0x02,
  Read             = 0x03,
  Write            = 0x04,
  QueryInformation = 0x05,
  SetInformation   = 0x06,
  DirectoryControl = 0x0C,
};
enum class IrpMinor : std::uint32_t {
  None           = 0x00,
  QueryDirectory = 0x01,
};
enum class InformationClass : std::uint32_t {
  Directory   = 1,
  Basic       = 4,
  Standard    = 5,
  Rename      = 10,
  Disposition = 13,
};
enum class FileAttribute : std::uint32_t {
  Directory = 0x10,
};
enum class AccessMask : std::uint32_t {
  None   = 0,
  Delete = 0x00010000,
};
}
namespace sdl_rdp::freerdp_facade {
using detail::rdpdr::AccessMask;
using detail::rdpdr::FileAttribute;
using detail::rdpdr::InformationClass;
using detail::rdpdr::IrpMajor;
using detail::rdpdr::IrpMinor;
}
