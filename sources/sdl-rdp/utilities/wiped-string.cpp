#include <sdl-rdp/utilities/wiped-string.hpp>

#include <memory>
#include <utility>

namespace sdl_rdp::utilities::detail::wiped_string {
auto Wipe(std::span<std::byte> bytes) noexcept -> void {
  for (volatile std::byte& byte : bytes) byte = std::byte{ };
}
WipedString::WipedString(std::string_view text, Allocator allocator) : _text{ text, allocator } { }
// A moved std::string keeps its resource and may leave a short text's bytes in its inline buffer.
WipedString::WipedString(WipedString&& other) noexcept : _text{ std::move(other._text) } {
  other.WipeStorage();
}
WipedString::~WipedString() {
  Clear();
}
auto WipedString::operator=(WipedString const& other) -> WipedString& {
  if (this == &other) return *this;
  Clear();
  _text = other._text;
  return *this;
}
// std::pmr::string's move assignment copies across resources; reconstruction moves the buffer with its resource.
auto WipedString::operator=(WipedString&& other) noexcept -> WipedString& {
  if (this == &other) return *this;
  Clear();
  std::destroy_at(&_text);
  std::construct_at(&_text, std::move(other._text));
  other.WipeStorage();
  return *this;
}
auto WipedString::Text() const noexcept -> std::string_view {
  return _text;
}
auto WipedString::Clear() noexcept -> void {
  Wipe(std::as_writable_bytes(std::span{ _text }));
  _text.clear();
}
auto WipedString::WipeStorage() noexcept -> void {
  _text.resize(_text.capacity());
  Clear();
}
}
