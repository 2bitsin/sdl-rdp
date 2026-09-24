#pragma once
#include <sdl-rdp/storage/drive-packet.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Backend {
enum class FileKind{ File, Directory, Any };
class FileRequest {
public:
       FileRequest(std::uint32_t flags, FileKind kind, std::uint32_t extra_access = 0);
  auto Create(std::span<std::byte const> name) const -> DrivePacket;

private:
  std::uint32_t _access;
  std::uint32_t _disposition;
  FileKind      _kind;
};
} // namespace Backend
