#pragma once
#include <cstdint>
#include <string>

namespace sdl_rdp::drive::detail::directory_entry {
struct DirectoryEntry {
  std::string   name;
  std::uint64_t size     { };
  bool          directory{ };
  friend auto operator==(DirectoryEntry const&, DirectoryEntry const&) -> bool = default;
};
}

namespace sdl_rdp::drive {
using detail::directory_entry::DirectoryEntry;
}
