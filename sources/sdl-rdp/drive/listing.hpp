#pragma once
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/drive/records.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace sdl_rdp::drive::detail::listing {
// A directory read into entries, "." and ".." dropped, the first offset entries skipped, at most limit kept.
class Listing {
public:
       Listing(std::size_t offset, std::size_t limit) noexcept;
  auto Collect(DrivePacket response) -> bool;
  auto Full() const noexcept         -> bool;
  auto Entries() &&                  -> std::vector<DirectoryEntry>;

private:
  auto Take(DirectoryEntry entry) -> void;
  std::vector<DirectoryEntry> _entries;
  std::size_t                 _offset;
  std::size_t                 _limit;
  std::size_t                 _skipped{ };
};
auto Entry(DrivePacket& packet)                                     -> DirectoryEntry;
auto DirectoryQuery(bool first, std::span<std::byte const> pattern) -> DrivePacket;
}

namespace sdl_rdp::drive {
using detail::listing::DirectoryQuery;
using detail::listing::Entry;
using detail::listing::Listing;
}
