#pragma once
#include <string_view>
namespace rdp {
// An SDL_IOFromFile mode string as backend drive flags.
class FileMode {
public:
  explicit FileMode(std::string_view mode);
  auto     Flags() const   -> unsigned;
  auto     Reads() const   -> bool;
  auto     Writes() const  -> bool;
  auto     Appends() const -> bool;
private:
  static auto _Flags(std::string_view mode) -> unsigned;
  unsigned _flags;
  bool     _append;
};
}
