#include "filemode.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <algorithm>
namespace sdl3::rdp::storage::detail::filemode {
using sdl_rdp::utilities::Expects;

namespace {
auto BaseAccess(std::string_view mode) -> FileAccess {
  Expects(!mode.empty(), "a file mode names its access");
  switch (mode.front()) {
  case 'r': return { .read = true };
  case 'w': return { .write = true, .create = true, .truncate = true };
  case 'a': return { .write = true, .create = true };
  default:  throw InvalidFileMode{ mode };
  }
}
}
FileMode::FileMode(std::string_view mode) : _access{ AccessOf(mode) }, _append{ mode.front() == 'a' } { }
auto FileMode::Access() const -> FileAccess {
  return _access;
}
auto FileMode::Reads() const -> bool {
  return _access.read;
}
auto FileMode::Writes() const -> bool {
  return _access.write;
}
auto FileMode::Appends() const -> bool {
  return _append;
}
auto FileMode::AccessOf(std::string_view mode) -> FileAccess {
  auto const modifiers = mode.empty() ? mode : mode.substr(1);
  if (mode.empty() || !std::ranges::all_of(modifiers, [](char value) { return value == '+' || value == 'b'; }))
    throw InvalidFileMode{ mode };
  auto access = BaseAccess(mode);
  if (modifiers.contains('+')) {
    access.read  = true;
    access.write = true;
  }
  return access;
}
}
