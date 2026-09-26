#pragma once
#include <cstdint>
#include <string>

namespace sdl_rdp::drive::detail::records {
struct Drive {
  std::uint32_t id  { };
  std::string   name;
  friend auto operator==(Drive const&, Drive const&) -> bool = default;
};
struct DirectoryEntry {
  std::string   name;
  std::uint64_t size     { };
  bool          directory{ };
  friend auto operator==(DirectoryEntry const&, DirectoryEntry const&) -> bool = default;
};
struct FileAccess {
  bool read    { };
  bool write   { };
  bool create  { };
  bool truncate{ };
};
struct FileStatus {
  std::uint64_t size     { };
  bool          directory{ };
  std::int64_t  modified { };
};
}

namespace sdl_rdp::drive {
using detail::records::DirectoryEntry;
using detail::records::Drive;
using detail::records::FileAccess;
using detail::records::FileStatus;
}
