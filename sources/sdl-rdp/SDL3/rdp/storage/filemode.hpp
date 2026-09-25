#pragma once
#include <cstdint>
#include <string_view>
namespace sdl3::rdp::storage::detail::filemode {
// An SDL_IOFromFile mode string as backend drive flags.
class FileMode {
public:
  explicit FileMode(std::string_view mode);
  auto     Flags() const   -> std::uint32_t;
  auto     Reads() const   -> bool;
  auto     Writes() const  -> bool;
  auto     Appends() const -> bool;
private:
  static auto FlagsOf(std::string_view mode) -> std::uint32_t;
  std::uint32_t _flags;
  bool          _append;
};
}
namespace sdl3::rdp::storage {
using detail::filemode::FileMode;
}
