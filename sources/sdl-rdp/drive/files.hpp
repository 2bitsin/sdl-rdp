#pragma once
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/directory-entry.hpp>
#include <sdl-rdp/drive/drive.hpp>
#include <sdl-rdp/drive/file-access.hpp>
#include <sdl-rdp/drive/file-request.hpp>
#include <sdl-rdp/drive/file-status.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace sdl_rdp::drive::detail::files {
using sdl_rdp::freerdp_facade::InformationClass;

// The file operations over the current peer's drive channel, which may be absent.
class DriveFiles {
public:
  explicit DriveFiles(std::shared_ptr<DriveChannel> channel) noexcept;
  auto     List() const                                                                           -> std::vector<Drive>;
  auto     Open(std::uint32_t drive, std::string_view path, FileAccess access, FileKind kind) const
      -> std::unique_ptr<File>;
  auto     Attached(File& file) const                                                             -> File&;
  auto     Stat(std::uint32_t drive, std::string_view path) const                                 -> FileStatus;
  auto     Enumerate(std::uint32_t drive, std::string_view path, std::size_t offset, std::size_t limit) const
      -> std::vector<DirectoryEntry>;
  auto     MakeDirectory(std::uint32_t drive, std::string_view path) const                        -> void;
  auto     Remove(std::uint32_t drive, std::string_view path) const                               -> void;
  auto     Rename(std::uint32_t drive, std::string_view path, std::string_view destination) const -> void;

private:
  auto Channel() const -> std::shared_ptr<DriveChannel> const&;
  auto Opened(std::uint32_t drive, std::string_view path, FileRequest const& request) const -> std::unique_ptr<File>;
  auto SetInformation(std::uint32_t drive, std::string_view path, InformationClass type, DrivePacket const& body) const
      -> void;
  std::shared_ptr<DriveChannel> _channel;
};
}

namespace sdl_rdp::drive {
using detail::files::DriveFiles;
}
