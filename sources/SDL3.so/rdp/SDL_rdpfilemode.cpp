#include "SDL_rdpfilemode.hpp"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
#include <algorithm>
#include <stdexcept>
namespace rdp {
namespace {
auto AccessFlags(char access) -> unsigned {
  switch (access) {
  case 'r': return SDLRDP_FILE_READ;
  case 'w': return SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE;
  case 'a': return SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE;
  default: throw std::invalid_argument("Invalid RDP file mode");
  }
}
}
FileMode::FileMode(std::string_view mode) : _flags{ _Flags(mode) }, _append{ mode.front() == 'a' } { }
auto FileMode::Flags() const -> unsigned { return _flags; }
auto FileMode::Reads() const -> bool { return (_flags & SDLRDP_FILE_READ) != 0; }
auto FileMode::Writes() const -> bool { return (_flags & SDLRDP_FILE_WRITE) != 0; }
auto FileMode::Appends() const -> bool { return _append; }
auto FileMode::_Flags(std::string_view mode) -> unsigned {
  auto const modifiers = mode.empty() ? mode : mode.substr(1);
  if (mode.empty() || !std::ranges::all_of(modifiers, [](char value) { return value == '+' || value == 'b'; }))
    throw std::invalid_argument("Invalid RDP file mode");
  auto const update = modifiers.contains('+') ? SDLRDP_FILE_READ | SDLRDP_FILE_WRITE : 0u;
  return AccessFlags(mode.front()) | update;
}
}
