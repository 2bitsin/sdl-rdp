#pragma once
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace sdl_rdp::utilities::detail::wiped_string {
// Overwrites the bytes through a volatile view, which the optimiser cannot drop as a dead store.
auto Wipe(std::span<std::byte> bytes) noexcept -> void;

// Text wiped in place before its storage is released or emptied, so a password never lingers in memory.
class WipedString {
public:
  using Allocator = std::pmr::polymorphic_allocator<char>;

           WipedString()                         = default;
  explicit WipedString(std::string_view text, Allocator allocator = { });
           WipedString(WipedString const& other) = default;
           WipedString(WipedString&& other)      noexcept;
           ~WipedString();
  auto     operator=(WipedString const& other)     -> WipedString&;
  auto     operator=(WipedString&& other) noexcept -> WipedString&;
  auto     Text() const noexcept                   -> std::string_view;

private:
  auto Clear() noexcept       -> void;
  auto WipeStorage() noexcept -> void;
  std::pmr::string _text;
};
}

namespace sdl_rdp::utilities {
using detail::wiped_string::Wipe;
using detail::wiped_string::WipedString;
}
