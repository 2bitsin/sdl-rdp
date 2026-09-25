#pragma once
#include <cstdint>

namespace sdl_rdp::drive::detail::file_status {
struct FileStatus {
  std::uint64_t size     { };
  bool          directory{ };
  std::int64_t  modified { };
};
}

namespace sdl_rdp::drive {
using detail::file_status::FileStatus;
}
