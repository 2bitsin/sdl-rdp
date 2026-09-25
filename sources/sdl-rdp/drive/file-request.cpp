#include <sdl-rdp/drive/file-request.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <winpr/nt.h>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace sdl_rdp::drive::detail::file_request {
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Unreachable;
namespace {
auto Validated(FileAccess access) -> FileAccess {
  if (access.truncate && !access.write) throw InvalidOpenAccess{ "truncate without write access" };
  return access;
}
auto Access(FileAccess access, std::uint32_t extra) -> std::uint32_t {
  auto mask = extra | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  if (access.read) mask |= FILE_READ_DATA;
  if (access.write) mask |= FILE_WRITE_DATA;
  return mask;
}
auto Disposition(FileAccess access) -> std::uint32_t {
  if (access.truncate) return access.create ? FILE_OVERWRITE_IF : FILE_OVERWRITE;
  return access.create ? FILE_OPEN_IF : FILE_OPEN;
}
auto CreateOptions(FileKind kind) -> std::uint32_t {
  switch (kind) {
  case FileKind::Directory: return FILE_DIRECTORY_FILE;
  case FileKind::File:      return FILE_NON_DIRECTORY_FILE;
  case FileKind::Any:       return 0;
  default:                  Unreachable(kind);
  }
}
}
FileRequest::FileRequest(FileAccess access, FileKind file_kind, AccessMask extra_access)
    : _access{ Access(Validated(access), std::to_underlying(extra_access)) }, _disposition{ Disposition(access) },
      _kind{ file_kind } { }
auto FileRequest::Create(std::span<std::byte const> name) const -> DrivePacket {
  DrivePacket packet;
  packet.Write(_access);
  packet.Write(std::uint64_t{ 0 });
  packet.Write(std::uint32_t{ 0 });
  packet.Write(std::uint32_t{ FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE });
  packet.Write(_disposition);
  packet.Write(CreateOptions(_kind));
  packet.Write(Narrowed<std::uint32_t>(name.size()));
  packet.Append(name);
  return packet;
}
}
