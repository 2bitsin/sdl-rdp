#pragma once
#include <sdl-rdp/utilities/flags.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::rdpdr {
using sdl_rdp::utilities::Has;
using sdl_rdp::utilities::operator&;
using sdl_rdp::utilities::operator|;

// The constants the rdpdr channel carries (MS-RDPEFS 2.2, MS-FSCC 2.4 and 2.6, MS-SMB2 2.2.13, MS-ERREF 2.3).
inline constexpr std::string_view DriveChannelName   { "rdpdr" };
inline constexpr std::uint16_t    ProtocolMajor      = 0x0001;
inline constexpr std::uint16_t    ProtocolMinorRdp6x = 0x000C;
inline constexpr std::uint32_t    AllMajorFunctions  = 0xFFFF;
inline constexpr std::uint32_t    EnableAsyncIo      = 0x0001;
enum class Component : std::uint16_t {
  Core = 0x4472,
};
enum class PacketId : std::uint16_t {
  ServerAnnounce     = 0x496E,
  ClientIdConfirm    = 0x4343,
  ClientName         = 0x434E,
  DeviceListAnnounce = 0x4441,
  DeviceReply        = 0x6472,
  DeviceIoRequest    = 0x4952,
  DeviceIoCompletion = 0x4943,
  ServerCapability   = 0x5350,
  ClientCapability   = 0x4350,
  DeviceListRemove   = 0x444D,
  UserLoggedOn       = 0x554C,
};
enum class CapabilityType : std::uint16_t {
  General = 0x0001,
  Drive   = 0x0004,
};
enum class CapabilityVersion : std::uint32_t {
  V1 = 0x00000001,
  V2 = 0x00000002,
};
enum class ExtendedPdu : std::uint32_t {
  DeviceRemove      = 0x00000001,
  ClientDisplayName = 0x00000002,
  UserLoggedOn      = 0x00000004,
};
auto FlagSet(ExtendedPdu /*set*/) -> std::true_type;
enum class DeviceType : std::uint32_t {
  Filesystem = 0x00000008,
};
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
auto FlagSet(FileAttribute /*set*/) -> std::true_type;
enum class AccessMask : std::uint32_t {
  None           = 0,
  ReadData       = 0x00000001,
  WriteData      = 0x00000002,
  ReadAttributes = 0x00000080,
  Delete         = 0x00010000,
  Synchronize    = 0x00100000,
};
auto FlagSet(AccessMask /*set*/) -> std::true_type;
enum class ShareAccess : std::uint32_t {
  Read   = 0x00000001,
  Write  = 0x00000002,
  Delete = 0x00000004,
};
auto FlagSet(ShareAccess /*set*/) -> std::true_type;
enum class CreateDisposition : std::uint32_t {
  Open        = 0x00000001,
  OpenIf      = 0x00000003,
  Overwrite   = 0x00000004,
  OverwriteIf = 0x00000005,
};
enum class CreateOption : std::uint32_t {
  None             = 0,
  DirectoryFile    = 0x00000001,
  NonDirectoryFile = 0x00000040,
};
// The statuses the drive compares; a client may complete a request with any other NTSTATUS.
enum class NtStatus : std::uint32_t {
  Success      = 0x00000000,
  NoMoreFiles  = 0x80000006,
  EndOfFile    = 0xC0000011,
  NotSupported = 0xC00000BB,
};
auto Name(NtStatus status) -> std::string_view;
}

namespace sdl_rdp::freerdp_facade {
using detail::rdpdr::AccessMask;
using detail::rdpdr::AllMajorFunctions;
using detail::rdpdr::CapabilityType;
using detail::rdpdr::CapabilityVersion;
using detail::rdpdr::Component;
using detail::rdpdr::CreateDisposition;
using detail::rdpdr::CreateOption;
using detail::rdpdr::DeviceType;
using detail::rdpdr::DriveChannelName;
using detail::rdpdr::EnableAsyncIo;
using detail::rdpdr::ExtendedPdu;
using detail::rdpdr::FileAttribute;
using detail::rdpdr::InformationClass;
using detail::rdpdr::IrpMajor;
using detail::rdpdr::IrpMinor;
using detail::rdpdr::Name;
using detail::rdpdr::NtStatus;
using detail::rdpdr::PacketId;
using detail::rdpdr::ProtocolMajor;
using detail::rdpdr::ProtocolMinorRdp6x;
using detail::rdpdr::ShareAccess;
}
