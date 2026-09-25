#pragma once
#include <sdl-rdp/utilities/narrowed.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sdl_rdp::headless_client_test::utilities::detail::octets {
template <std::integral OctetTy> auto Octet(OctetTy octet) -> std::byte {
  if constexpr (std::same_as<OctetTy, char>)
    return std::bit_cast<std::byte>(octet);
  else
    return std::byte{ Backend::Narrowed<std::uint8_t>(octet) };
}
// The octets a rig puts on the wire, written as numbers or characters.
template <std::integral... OctetTy> auto Octets(OctetTy... octets) -> std::vector<std::byte> {
  return { Octet(octets)... };
}
// Clipboard text as CF_UNICODETEXT carries it: UTF-16LE with its terminator.
auto UnicodeText(std::string_view utf8) -> std::vector<std::byte>;
// Clipboard text as CF_TEXT carries it: the octets with their terminator.
auto AnsiText(std::string_view text) -> std::vector<std::byte>;
}
namespace sdl_rdp::headless_client_test::utilities {
using detail::octets::AnsiText;
using detail::octets::Octets;
using detail::octets::UnicodeText;
}
