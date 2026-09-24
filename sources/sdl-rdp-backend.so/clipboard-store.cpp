#include "_detail/clipboard-store.hpp"

#include "_detail/clipboard.hpp"

#include <utility>

namespace Backend {
uint64_t ClipboardStore::Replace(std::string value) {
  auto encoded = ClipboardUnicode(value);
  _text    = std::move(value);
  _unicode = std::move(encoded);
  return ++_generation;
}
std::string const& ClipboardStore::Text() const noexcept {
  return _text;
}
std::span<BYTE const> ClipboardStore::Unicode() const noexcept {
  return _unicode;
}
uint64_t ClipboardStore::Generation() const noexcept {
  return _generation;
}
char const* ClipboardStore::Export() {
  _exported = _text;
  return _exported.c_str();
}
}
