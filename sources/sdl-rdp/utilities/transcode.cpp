#include <sdl-rdp/utilities/transcode.hpp>

#include <oxbox/utilities/span.hpp>
#include <cstddef>
#include <span>
#include <vector>

namespace Backend {
auto Utf16(std::string_view utf8) -> std::u16string {
  using oxbox::utilities::Encoding;
  auto const bytes = TranscodeRange<std::vector<std::byte>>(
      oxbox::utilities::AsBytes(utf8), { }, { .encoding = Encoding::UTF16, .order = std::endian::native });
  auto const units = oxbox::utilities::SpanCast<char16_t const>(std::span(bytes));
  return { units.begin(), units.end() };
}
}
