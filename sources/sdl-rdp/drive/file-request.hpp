#pragma once
#include <sdl-rdp/drive/packet.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::drive::detail::file_request {
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
}
namespace sdl_rdp::drive {
using detail::file_request::FileKind;
using detail::file_request::FileRequest;
}
