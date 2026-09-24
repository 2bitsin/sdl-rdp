#pragma once
#include <sdl-rdp/storage/drive-packet.hpp>

#include <cstdint>
#include <span>

namespace Backend {
enum class FileKind{ File, Directory, Any };
class FileRequest {
public:
       FileRequest(unsigned flags, FileKind kind, unsigned extra_access = 0);
  auto Create(std::span<std::byte const> name) const -> DrivePacket;

private:
  unsigned _access;
  unsigned _disposition;
  FileKind _kind;
};
} // namespace Backend
