#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <winpr/wtypes.h>

namespace Backend {
class ClipboardStore {
public:
  uint64_t              Replace(std::string value);
  std::string const&    Text() const       noexcept;
  std::span<BYTE const> Unicode() const    noexcept;
  uint64_t              Generation() const noexcept;
  char const*           Export();

private:
  std::string       _text;
  std::string       _exported;
  std::vector<BYTE> _unicode   { 0, 0 };
  uint64_t          _generation{ };
};
}
