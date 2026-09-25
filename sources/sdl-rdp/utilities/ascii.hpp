#pragma once

namespace sdl_rdp::utilities::detail::ascii {
// ASCII only, never the locale's: identifiers and hint names are ASCII.
constexpr auto AsciiUpper(char letter) -> char {
  return letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
}
}
namespace sdl_rdp::utilities {
using detail::ascii::AsciiUpper;
}
