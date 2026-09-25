#include "filemode.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/contract.hpp>
#include <algorithm>
#include <cstdint>
namespace sdl3::rdp::storage::detail::filemode {
using sdl_rdp::utilities::Expects;

namespace {
auto AccessFlags(std::string_view mode) -> std::uint32_t {
  Expects(!mode.empty(), "a file mode names its access");
  switch (mode.front()) {
  case 'r': return SDLRDP_FILE_READ;
  case 'w': return SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE;
  case 'a': return SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE;
  default:  throw InvalidFileMode{ mode };
  }
}
}
FileMode::FileMode(std::string_view mode) : _flags{ FlagsOf(mode) }, _append{ mode.front() == 'a' } { }
auto FileMode::Flags() const -> std::uint32_t {
  return _flags;
}
auto FileMode::Reads() const -> bool {
  return (_flags & SDLRDP_FILE_READ) != 0;
}
auto FileMode::Writes() const -> bool {
  return (_flags & SDLRDP_FILE_WRITE) != 0;
}
auto FileMode::Appends() const -> bool {
  return _append;
}
auto FileMode::FlagsOf(std::string_view mode) -> std::uint32_t {
  auto const modifiers = mode.empty() ? mode : mode.substr(1);
  if (mode.empty() || !std::ranges::all_of(modifiers, [](char value) { return value == '+' || value == 'b'; }))
    throw InvalidFileMode{ mode };
  auto const update = modifiers.contains('+') ? SDLRDP_FILE_READ | SDLRDP_FILE_WRITE : 0u;
  return AccessFlags(mode) | update;
}
}
