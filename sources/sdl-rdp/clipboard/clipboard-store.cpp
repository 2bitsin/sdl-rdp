#include <sdl-rdp/clipboard/clipboard-store.hpp>

#include <sdl-rdp/clipboard/clipboard.hpp>

#include <utility>

namespace Backend {
auto ClipboardStore::Replace(std::string value) -> uint64_t {
  _unicode = ClipboardUnicode(value);
  return Generational::Replace(std::move(value));
}
auto ClipboardStore::Text() const noexcept -> std::string const& {
  return Value();
}
auto ClipboardStore::Unicode() const noexcept -> std::span<BYTE const> {
  return _unicode;
}
auto ClipboardStore::Export() -> std::string const& {
  _exported = Value();
  return _exported;
}
}
