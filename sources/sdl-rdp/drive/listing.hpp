#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/drive/packet.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::drive::detail::listing {
// A directory read into the caller's entries, "." and ".." dropped, the first offset entries skipped.
class Listing {
public:
       Listing(std::uint32_t offset, std::span<sdlrdp_dirent> out) noexcept;
  auto Collect(DrivePacket response) -> bool;
  auto Full() const noexcept         -> bool;
  auto Count() const noexcept        -> std::size_t;

private:
  auto Take(sdlrdp_dirent const& entry) -> void;
  std::span<sdlrdp_dirent> _out;
  std::uint32_t            _offset;
  std::size_t              _skipped{ };
  std::size_t              _count  { };
};
auto Entry(DrivePacket& packet)                                     -> sdlrdp_dirent;
auto DirectoryQuery(bool first, std::span<std::byte const> pattern) -> DrivePacket;
}
namespace sdl_rdp::drive {
using detail::listing::DirectoryQuery;
using detail::listing::Entry;
using detail::listing::Listing;
}
