#pragma once

namespace sdl_rdp::drive::detail::file_access {
struct FileAccess {
  bool read    { };
  bool write   { };
  bool create  { };
  bool truncate{ };
};
}

namespace sdl_rdp::drive {
using detail::file_access::FileAccess;
}
