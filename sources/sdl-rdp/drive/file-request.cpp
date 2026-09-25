#include <sdl-rdp/drive/file-request.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <sdl-rdp/abi/backend.h>

#include <winpr/nt.h>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::drive::detail::file_request {
using Backend::Narrowed;
namespace {
constexpr std::uint32_t AllowedFlags = SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE
                                       | SDLRDP_FILE_DIRECTORY;
auto Validated(std::uint32_t flags) -> std::uint32_t {
  if ((flags & SDLRDP_FILE_TRUNCATE) && !(flags & SDLRDP_FILE_WRITE))
    throw InvalidOpenFlags{ flags, "truncate without write access" };
  if (flags & ~AllowedFlags) throw InvalidOpenFlags{ flags, "unknown bits" };
  return flags;
}
auto Access(std::uint32_t flags, std::uint32_t extra) -> std::uint32_t {
  auto access = extra | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  if (flags & SDLRDP_FILE_READ) access |= FILE_READ_DATA;
  if (flags & SDLRDP_FILE_WRITE) access |= FILE_WRITE_DATA;
  return access;
}
auto Disposition(std::uint32_t flags) -> std::uint32_t {
  bool const create = flags & SDLRDP_FILE_CREATE;
  if (flags & SDLRDP_FILE_TRUNCATE) return create ? FILE_OVERWRITE_IF : FILE_OVERWRITE;
  return create ? FILE_OPEN_IF : FILE_OPEN;
}
auto CreateOptions(FileKind kind) -> std::uint32_t {
  switch (kind) {
  case FileKind::Directory: return FILE_DIRECTORY_FILE;
  case FileKind::File:      return FILE_NON_DIRECTORY_FILE;
  case FileKind::Any:       return 0;
  default:                  utilities::Unreachable(kind);
  }
}
}
FileRequest::FileRequest(std::uint32_t flags, FileKind file_kind, std::uint32_t extra_access)
    : _access{ Access(Validated(flags), extra_access) }, _disposition{ Disposition(flags) }, _kind{ file_kind } { }
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
