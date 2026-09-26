#pragma once
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/file-status.hpp>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace sdl_rdp::drive::detail::file {
using sdl_rdp::freerdp_facade::IrpMajor;
using sdl_rdp::freerdp_facade::IrpMinor;
using sdl_rdp::utilities::Pinned;

// An open file on a redirected drive; closing it is a request the peer answers, so the destructor contains it.
class File : private Pinned {
public:
       File(std::shared_ptr<DriveChannel> source, std::uint32_t device, std::uint32_t file, std::string name);
       ~File();
  auto Close()                                                          -> void;
  auto Channel() const                                                  -> std::shared_ptr<DriveChannel> const&;
  auto Drive() const                                                    -> std::uint32_t;
  auto Id() const                                                       -> std::uint32_t;
  auto Path() const                                                     -> std::string const&;
  auto Transfer(std::uint64_t offset, std::span<std::byte> bytes)       -> std::size_t;
  auto Transfer(std::uint64_t offset, std::span<std::byte const> bytes) -> std::size_t;
  auto Stat()                                                           -> FileStatus;

private:
  template <class ByteTy> auto Checked(std::uint64_t offset, std::span<ByteTy> bytes) -> std::size_t;
  std::shared_ptr<DriveChannel> _channel;
  std::uint32_t                 _drive;
  std::uint32_t                 _wire;
  std::string                   _path;
  bool                          _closed { };
};
auto Exchange(File& file, IrpMajor major, DrivePacket const& packet, IrpMinor minor = IrpMinor::None, bool end = false)
    -> DrivePacket;
}

namespace sdl_rdp::drive {
using detail::file::Exchange;
using detail::file::File;
}
