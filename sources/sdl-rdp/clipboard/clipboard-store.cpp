#include <sdl-rdp/clipboard/clipboard-store.hpp>

#include <sdl-rdp/clipboard/clipboard.hpp>

#include <utility>

namespace Backend {
auto ClipboardStore::Replace(std::string value) -> uint64_t {
  auto encoded = ClipboardUnicode(value);
  _text    = std::move(value);
  _unicode = std::move(encoded);
  return ++_generation;
}
auto ClipboardStore::Text() const noexcept -> std::string const& {
  return _text;
}
auto ClipboardStore::Unicode() const noexcept -> std::span<BYTE const> {
  return _unicode;
}
auto ClipboardStore::Generation() const noexcept -> uint64_t {
  return _generation;
}
auto ClipboardStore::Export() -> char const* {
  _exported = _text;
  return _exported.c_str();
}
}
