#pragma once
#include <cstdint>
#include <string_view>
namespace rdp {
// An SDL_IOFromFile mode string as backend drive flags.
class FileMode {
public:
  explicit FileMode(std::string_view mode);
  auto     Flags() const   -> std::uint32_t;
  auto     Reads() const   -> bool;
  auto     Writes() const  -> bool;
  auto     Appends() const -> bool;
private:
  static auto _Flags(std::string_view mode) -> std::uint32_t;
  std::uint32_t _flags;
  bool          _append;
};
}
