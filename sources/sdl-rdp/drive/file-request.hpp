#pragma once
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/drive/records.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::drive::detail::file_request {
using sdl_rdp::freerdp_facade::AccessMask;

enum class FileKind{ File, Directory, Any };
class FileRequest {
public:
       FileRequest(FileAccess access, FileKind kind, AccessMask extra_access = AccessMask::None);
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
