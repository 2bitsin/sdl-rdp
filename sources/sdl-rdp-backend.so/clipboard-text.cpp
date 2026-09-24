#include "_detail/clipboard.hpp"
#include "_detail/transcode.hpp"

#include <algorithm>
#include <ranges>

namespace Backend {
using oxbox::utilities::Encoding;
auto ClipboardAnsi(std::string_view text) -> std::string {
  // No client code page is negotiated; ASCII is portable across ANSI code pages.
  return TranscodeRange<std::string>(std::as_bytes(std::span(text)), { }, { .encoding = Encoding::UCS1 },
                                     [](char32_t point) { return point < 128 ? point : U'?'; });
}
auto ClipboardUnicode(std::string_view text) -> std::vector<BYTE> {
  if (text.size() > UINT32_MAX / 2 - 1) throw std::runtime_error("Clipboard text is too large.");
  auto encoded = TranscodeRange<std::vector<BYTE>>(std::as_bytes(std::span(text)), { }, Utf16Little);
  encoded.resize(encoded.size() + sizeof(char16_t));
  return encoded;
}
auto ClipboardUtf8(std::span<BYTE const> bytes) -> std::string {
  if (bytes.size() < sizeof(char16_t) || bytes.size() % sizeof(char16_t))
    throw std::runtime_error("Invalid UTF-16LE clipboard length.");
  auto units = bytes | std::views::chunk(sizeof(char16_t));
  auto end   = std::ranges::find_if(units, [](auto unit) { return unit[0] == 0 && unit[1] == 0; });
  if (end == units.end()) throw std::runtime_error("Clipboard text lacks a terminator.");
  return TranscodeRange<std::string>(std::as_bytes(bytes.first((end - units.begin()) * sizeof(char16_t))), Utf16Little,
                                     { });
}
}
