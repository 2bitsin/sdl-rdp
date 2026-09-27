#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <freerdp/channels/rdpdr.h>
#include <winpr/file.h>
#include <winpr/nt.h>
#include <bit>
#include <cstdint>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::rdpdr {
static_assert(DriveChannelName == RDPDR_SVC_CHANNEL_NAME);
static_assert(DriveChannelName == RDPDR_CHANNEL_NAME);
static_assert(ProtocolMajor == RDPDR_VERSION_MAJOR);
static_assert(ProtocolMinorRdp6x == RDPDR_VERSION_MINOR_RDP6X);
static_assert(AllMajorFunctions
              == (RDPDR_IRP_MJ_CREATE | RDPDR_IRP_MJ_CLEANUP | RDPDR_IRP_MJ_CLOSE | RDPDR_IRP_MJ_READ
                  | RDPDR_IRP_MJ_WRITE | RDPDR_IRP_MJ_FLUSH_BUFFERS | RDPDR_IRP_MJ_SHUTDOWN
                  | RDPDR_IRP_MJ_DEVICE_CONTROL | RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION
                  | RDPDR_IRP_MJ_SET_VOLUME_INFORMATION | RDPDR_IRP_MJ_QUERY_INFORMATION | RDPDR_IRP_MJ_SET_INFORMATION
                  | RDPDR_IRP_MJ_DIRECTORY_CONTROL | RDPDR_IRP_MJ_LOCK_CONTROL | RDPDR_IRP_MJ_QUERY_SECURITY
                  | RDPDR_IRP_MJ_SET_SECURITY));
static_assert(EnableAsyncIo == ENABLE_ASYNCIO);
static_assert(std::to_underlying(Component::Core) == RDPDR_CTYP_CORE);
static_assert(std::to_underlying(PacketId::ServerAnnounce) == PAKID_CORE_SERVER_ANNOUNCE);
static_assert(std::to_underlying(PacketId::ClientIdConfirm) == PAKID_CORE_CLIENTID_CONFIRM);
static_assert(std::to_underlying(PacketId::ClientName) == PAKID_CORE_CLIENT_NAME);
static_assert(std::to_underlying(PacketId::DeviceListAnnounce) == PAKID_CORE_DEVICELIST_ANNOUNCE);
static_assert(std::to_underlying(PacketId::DeviceReply) == PAKID_CORE_DEVICE_REPLY);
static_assert(std::to_underlying(PacketId::DeviceIoRequest) == PAKID_CORE_DEVICE_IOREQUEST);
static_assert(std::to_underlying(PacketId::DeviceIoCompletion) == PAKID_CORE_DEVICE_IOCOMPLETION);
static_assert(std::to_underlying(PacketId::ServerCapability) == PAKID_CORE_SERVER_CAPABILITY);
static_assert(std::to_underlying(PacketId::ClientCapability) == PAKID_CORE_CLIENT_CAPABILITY);
static_assert(std::to_underlying(PacketId::DeviceListRemove) == PAKID_CORE_DEVICELIST_REMOVE);
static_assert(std::to_underlying(PacketId::UserLoggedOn) == PAKID_CORE_USER_LOGGEDON);
static_assert(std::to_underlying(CapabilityType::General) == CAP_GENERAL_TYPE);
static_assert(std::to_underlying(CapabilityType::Drive) == CAP_DRIVE_TYPE);
static_assert(std::to_underlying(CapabilityVersion::V1) == GENERAL_CAPABILITY_VERSION_01);
static_assert(std::to_underlying(CapabilityVersion::V1) == DRIVE_CAPABILITY_VERSION_01);
static_assert(std::to_underlying(CapabilityVersion::V2) == GENERAL_CAPABILITY_VERSION_02);
static_assert(std::to_underlying(CapabilityVersion::V2) == DRIVE_CAPABILITY_VERSION_02);
static_assert(std::to_underlying(ExtendedPdu::DeviceRemove) == RDPDR_DEVICE_REMOVE_PDUS);
static_assert(std::to_underlying(ExtendedPdu::ClientDisplayName) == RDPDR_CLIENT_DISPLAY_NAME_PDU);
static_assert(std::to_underlying(ExtendedPdu::UserLoggedOn) == RDPDR_USER_LOGGEDON_PDU);
static_assert(std::to_underlying(DeviceType::Filesystem) == RDPDR_DTYP_FILESYSTEM);
static_assert(std::to_underlying(IrpMajor::Create) == IRP_MJ_CREATE);
static_assert(std::to_underlying(IrpMajor::Close) == IRP_MJ_CLOSE);
static_assert(std::to_underlying(IrpMajor::Read) == IRP_MJ_READ);
static_assert(std::to_underlying(IrpMajor::Write) == IRP_MJ_WRITE);
static_assert(std::to_underlying(IrpMajor::QueryInformation) == IRP_MJ_QUERY_INFORMATION);
static_assert(std::to_underlying(IrpMajor::SetInformation) == IRP_MJ_SET_INFORMATION);
static_assert(std::to_underlying(IrpMajor::DirectoryControl) == IRP_MJ_DIRECTORY_CONTROL);
static_assert(std::to_underlying(IrpMinor::QueryDirectory) == IRP_MN_QUERY_DIRECTORY);
static_assert(std::to_underlying(InformationClass::Directory) == FileDirectoryInformation);
static_assert(std::to_underlying(InformationClass::Basic) == FileBasicInformation);
static_assert(std::to_underlying(InformationClass::Standard) == FileStandardInformation);
static_assert(std::to_underlying(InformationClass::Rename) == FileRenameInformation);
static_assert(std::to_underlying(InformationClass::Disposition) == FileDispositionInformation);
static_assert(std::to_underlying(FileAttribute::Directory) == FILE_ATTRIBUTE_DIRECTORY);
static_assert(std::to_underlying(AccessMask::ReadData) == FILE_READ_DATA);
static_assert(std::to_underlying(AccessMask::WriteData) == FILE_WRITE_DATA);
static_assert(std::to_underlying(AccessMask::ReadAttributes) == FILE_READ_ATTRIBUTES);
static_assert(std::to_underlying(AccessMask::Delete) == DELETE);
static_assert(std::to_underlying(AccessMask::Synchronize) == SYNCHRONIZE);
static_assert(std::to_underlying(ShareAccess::Read) == FILE_SHARE_READ);
static_assert(std::to_underlying(ShareAccess::Write) == FILE_SHARE_WRITE);
static_assert(std::to_underlying(ShareAccess::Delete) == FILE_SHARE_DELETE);
static_assert(std::to_underlying(CreateDisposition::Open) == FILE_OPEN);
static_assert(std::to_underlying(CreateDisposition::OpenIf) == FILE_OPEN_IF);
static_assert(std::to_underlying(CreateDisposition::Overwrite) == FILE_OVERWRITE);
static_assert(std::to_underlying(CreateDisposition::OverwriteIf) == FILE_OVERWRITE_IF);
static_assert(std::to_underlying(CreateOption::DirectoryFile) == FILE_DIRECTORY_FILE);
static_assert(std::to_underlying(CreateOption::NonDirectoryFile) == FILE_NON_DIRECTORY_FILE);
static_assert(std::to_underlying(NtStatus::Success) == std::bit_cast<std::uint32_t>(STATUS_SUCCESS));
static_assert(std::to_underlying(NtStatus::NoMoreFiles) == std::bit_cast<std::uint32_t>(STATUS_NO_MORE_FILES));
static_assert(std::to_underlying(NtStatus::EndOfFile) == std::bit_cast<std::uint32_t>(STATUS_END_OF_FILE));
static_assert(std::to_underlying(NtStatus::NotSupported) == std::bit_cast<std::uint32_t>(STATUS_NOT_SUPPORTED));

auto Name(NtStatus status) -> std::string_view {
  // WinPR owns the NTSTATUS name table; a value it does not name keeps only its code.
  auto const* name = NtStatus2Tag(std::bit_cast<std::int32_t>(std::to_underlying(status)));
  return name ? std::string_view{ name } : std::string_view{ "unknown NTSTATUS" };
}
}
