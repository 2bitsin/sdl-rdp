#pragma once
#include <sdl-rdp/utilities/generational.hpp>

#include <winpr/wtypes.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Backend {
class ClipboardStore : private Generational<std::string> {
public:
  using Generational::Generation;
  auto Replace(std::string value) -> uint64_t;
  auto Text() const noexcept      -> std::string const&;
  auto Unicode() const noexcept   -> std::span<std::uint8_t const>;
  auto Export()                   -> std::string const&;

private:
  std::string               _exported;
  std::vector<std::uint8_t> _unicode { 0, 0 };
};
}
