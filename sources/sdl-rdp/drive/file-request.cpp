#include <sdl-rdp/drive/file-request.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::drive::detail::file_request {
using sdl_rdp::freerdp_facade::CreateDisposition;
using sdl_rdp::freerdp_facade::CreateOption;
using sdl_rdp::freerdp_facade::ShareAccess;
using sdl_rdp::utilities::Unreachable;
namespace {
auto Validated(FileAccess access) -> FileAccess {
  if (access.truncate && !access.write) throw InvalidOpenAccess{ "truncate without write access" };
  return access;
}
auto Access(FileAccess access, AccessMask extra) -> AccessMask {
  return extra | AccessMask::ReadAttributes | AccessMask::Synchronize
         | (access.read ? AccessMask::ReadData : AccessMask::None)
         | (access.write ? AccessMask::WriteData : AccessMask::None);
}
auto Disposition(FileAccess access) -> CreateDisposition {
  if (access.truncate) return access.create ? CreateDisposition::OverwriteIf : CreateDisposition::Overwrite;
  return access.create ? CreateDisposition::OpenIf : CreateDisposition::Open;
}
auto CreateOptions(FileKind kind) -> CreateOption {
  switch (kind) {
  case FileKind::Directory: return CreateOption::DirectoryFile;
  case FileKind::File:      return CreateOption::NonDirectoryFile;
  case FileKind::Any:       return CreateOption::None;
  default:                  Unreachable(kind);
  }
}
}
FileRequest::FileRequest(FileAccess access, FileKind file_kind, AccessMask extra_access)
    : _access{ Access(Validated(access), extra_access) }, _disposition{ Disposition(access) }, _kind{ file_kind } { }
auto FileRequest::Create(std::span<std::byte const> name) const -> DrivePacket {
  DrivePacket packet;
  packet.Write(_access);
  packet.Write(std::uint64_t{ 0 });
  packet.Write(std::uint32_t{ 0 });
  packet.Write(ShareAccess::Read | ShareAccess::Write | ShareAccess::Delete);
  packet.Write(_disposition);
  packet.Write(CreateOptions(_kind));
  packet.AppendCounted(name);
  return packet;
}
}
