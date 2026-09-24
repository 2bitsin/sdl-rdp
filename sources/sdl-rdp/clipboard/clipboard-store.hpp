#pragma once
#include <winpr/wtypes.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Backend {
class ClipboardStore {
public:
  auto Replace(std::string value)  -> uint64_t;
  auto Text() const noexcept       -> std::string const&;
  auto Unicode() const noexcept    -> std::span<BYTE const>;
  auto Generation() const noexcept -> uint64_t;
  auto Export()                    -> char const*;

private:
  std::string       _text;
  std::string       _exported;
  std::vector<BYTE> _unicode   { 0, 0 };
  uint64_t          _generation{ };
};
}
