#include <sdl-rdp/clipboard/text.hpp>

#include <sdl-rdp/clipboard/channel.hpp>
#include <sdl-rdp/clipboard/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::clipboard::detail::text {
using oxbox::utilities::Encoding;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::TranscodeRange;
using sdl_rdp::utilities::Utf16Little;
auto ClipboardAnsi(std::string_view text) -> std::string {
  // No client code page is negotiated; ASCII is portable across ANSI code pages.
  return TranscodeRange<std::string>(std::as_bytes(std::span(text)), { }, { .encoding = Encoding::UCS1 },
                                     [](char32_t point) { return point < 128 ? point : U'?'; });
}
auto ClipboardUnicode(std::string_view text) -> std::vector<std::byte> {
  if (text.size() > UINT32_MAX / 2 - 1) throw ClipboardTextTooLarge{ text.size() };
  auto encoded = TranscodeRange<std::vector<std::byte>>(std::as_bytes(std::span(text)), { }, Utf16Little);
  encoded.resize(encoded.size() + sizeof(char16_t));
  return encoded;
}
auto ClipboardUtf8(std::span<std::byte const> bytes) -> std::string {
  if (bytes.size() < sizeof(char16_t) || bytes.size() % sizeof(char16_t)) throw InvalidClipboardLength{ bytes.size() };
  auto units = bytes | std::views::chunk(sizeof(char16_t));
  auto end   = std::ranges::find_if(units,
                                    [](auto unit) { return unit[0] == std::byte{ 0 } && unit[1] == std::byte{ 0 }; });
  if (end == units.end()) throw UnterminatedClipboard{ };
  return TranscodeRange<std::string>(bytes.first(Narrowed<std::size_t>(end - units.begin()) * sizeof(char16_t)),
                                     Utf16Little, { });
}
}
