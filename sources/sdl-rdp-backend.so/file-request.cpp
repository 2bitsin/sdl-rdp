#include "_detail/file-request.hpp"

#include "sdl-rdp-backend.h"

#include <stdexcept>
#include <winpr/nt.h>

namespace Backend {
namespace {
constexpr unsigned AllowedFlags =
    SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE | SDLRDP_FILE_DIRECTORY;
unsigned Validated(unsigned flags) {
  if ((flags & SDLRDP_FILE_TRUNCATE) && !(flags & SDLRDP_FILE_WRITE))
    throw std::runtime_error("Truncate requires write access.");
  if (flags & ~AllowedFlags) throw std::runtime_error("Invalid drive open flags.");
  return flags;
}
unsigned Access(unsigned flags, unsigned extra) {
  auto access = extra | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  if (flags & SDLRDP_FILE_READ) access |= FILE_READ_DATA;
  if (flags & SDLRDP_FILE_WRITE) access |= FILE_WRITE_DATA;
  return access;
}
unsigned Disposition(unsigned flags) {
  bool const create = flags & SDLRDP_FILE_CREATE;
  if (flags & SDLRDP_FILE_TRUNCATE) return create ? FILE_OVERWRITE_IF : FILE_OVERWRITE;
  return create ? FILE_OPEN_IF : FILE_OPEN;
}
unsigned CreateOptions(FileKind kind) {
  switch (kind) {
  case FileKind::Directory:
    return FILE_DIRECTORY_FILE;
  case FileKind::File:
    return FILE_NON_DIRECTORY_FILE;
  case FileKind::Any:
    return 0;
  default:
    utilities::Unreachable(kind);
  }
}
}
FileRequest::FileRequest(unsigned flags, FileKind file_kind, unsigned extra_access)
    : _access { Access(Validated(flags), extra_access) }, _disposition{ Disposition(flags) }, _kind{ file_kind } { }
DrivePacket FileRequest::Create(std::span<uint8_t const> name) const {
  DrivePacket packet;
  packet.Put(_access);
  packet.Put(0, 8);
  packet.Put(0);
  packet.Put(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  packet.Put(_disposition);
  packet.Put(CreateOptions(_kind));
  packet.Put(name.size());
  packet.Append(name);
  return packet;
}
} // namespace Backend
