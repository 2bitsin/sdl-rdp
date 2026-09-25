#include <sdl-rdp/clipboard/store.hpp>

#include <sdl-rdp/clipboard/channel.hpp>
#include <sdl-rdp/clipboard/text.hpp>

#include <cstdint>
#include <utility>

namespace sdl_rdp::clipboard::detail::store {
auto ClipboardStore::Replace(std::string value) -> std::uint64_t {
  _unicode = ClipboardUnicode(value);
  return Generational::Replace(std::move(value));
}
auto ClipboardStore::Text() const noexcept -> std::string const& {
  return Value();
}
auto ClipboardStore::Unicode() const noexcept -> std::span<std::byte const> {
  return _unicode;
}
}
