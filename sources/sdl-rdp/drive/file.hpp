#pragma once
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct sdlrdp_file {
public:
       sdlrdp_file(sdlrdp_file const&)                                                  = delete;
       sdlrdp_file(sdlrdp_file&&)                                                       = delete;
       sdlrdp_file(std::shared_ptr<sdl_rdp::drive::DriveChannel> source, std::uint32_t device, std::uint32_t file,
                   std::string name);
       ~sdlrdp_file();
  auto operator=(sdlrdp_file const&)                                    -> sdlrdp_file& = delete;
  auto operator=(sdlrdp_file&&)                                         -> sdlrdp_file& = delete;
  auto Close()                                                          -> void;
  auto Channel() const -> std::shared_ptr<sdl_rdp::drive::DriveChannel> const&;
  auto Drive() const                                                    -> std::uint32_t;
  auto Id() const                                                       -> std::uint32_t;
  auto Path() const                                                     -> std::string const&;
  auto Transfer(std::uint64_t offset, std::span<std::byte> bytes)       -> int;
  auto Transfer(std::uint64_t offset, std::span<std::byte const> bytes) -> int;
  auto Stat()                                                           -> sdlrdp_stat;

private:
  template <class ByteTy> auto Checked(std::uint64_t offset, std::span<ByteTy> bytes) -> int;
  std::shared_ptr<sdl_rdp::drive::DriveChannel> channel;
  std::uint32_t                                 drive;
  std::uint32_t                                 wire;
  std::string                                   path;
  bool                                          closed { };
};
namespace sdl_rdp::drive::detail::file {
using sdl_rdp::freerdp_facade::IrpMajor;
using sdl_rdp::freerdp_facade::IrpMinor;

auto Exchange(sdlrdp_file& file, IrpMajor major, DrivePacket const& packet, IrpMinor minor = IrpMinor::None,
              bool end = false) -> DrivePacket;
}

namespace sdl_rdp::drive {
using detail::file::Exchange;
}
