#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <freerdp/channels/rdpdr.h>
#include <winpr/file.h>
#include <winpr/nt.h>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::rdpdr {
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
static_assert(std::to_underlying(AccessMask::Delete) == DELETE);
}
