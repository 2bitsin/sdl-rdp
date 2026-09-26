#pragma once
#include <sdl-rdp/drive/records.hpp>
#include <string_view>
namespace sdl3::rdp::storage::detail::filemode {
using sdl_rdp::drive::FileAccess;

// An SDL_IOFromFile mode string as drive file access.
class FileMode {
public:
  explicit FileMode(std::string_view mode);
  auto     Access() const  -> FileAccess;
  auto     Appends() const -> bool;
private:
  static auto AccessOf(std::string_view mode) -> FileAccess;
  FileAccess _access;
  bool       _append;
};
}

namespace sdl3::rdp::storage {
using detail::filemode::FileMode;
}
