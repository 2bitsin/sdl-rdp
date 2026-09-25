#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/file-request.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace sdl_rdp::drive::detail::files {
using sdl_rdp::freerdp_facade::InformationClass;

// The file operations of the drive ABI over the current peer's drive channel, which may be absent.
class DriveFiles {
public:
  explicit DriveFiles(std::shared_ptr<DriveChannel> channel) noexcept;
  auto     List(std::span<sdlrdp_drive> out) const                                                -> int;
  auto Open(std::uint32_t drive, std::string_view path, std::uint32_t flags) const -> std::unique_ptr<sdlrdp_file>;
  auto     Attached(sdlrdp_file& file) const                                                      -> sdlrdp_file&;
  auto     Stat(std::uint32_t drive, std::string_view path) const                                 -> sdlrdp_stat;
  auto Enumerate(std::uint32_t drive, std::string_view path, std::uint32_t offset, std::span<sdlrdp_dirent> out) const
      -> int;
  auto     MakeDirectory(std::uint32_t drive, std::string_view path) const                        -> void;
  auto     Remove(std::uint32_t drive, std::string_view path) const                               -> void;
  auto     Rename(std::uint32_t drive, std::string_view path, std::string_view destination) const -> void;

private:
  auto Channel() const -> std::shared_ptr<DriveChannel> const&;
  auto Opened(std::uint32_t drive, std::string_view path, FileRequest const& request) const
      -> std::unique_ptr<sdlrdp_file>;
  auto SetInformation(std::uint32_t drive, std::string_view path, InformationClass type, DrivePacket const& body) const
      -> void;
  std::shared_ptr<DriveChannel> _channel;
};
}

namespace sdl_rdp::drive {
using detail::files::DriveFiles;
}
