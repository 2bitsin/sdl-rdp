#include <sdl-rdp/utilities/text.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::utilities::detail::text {
auto CopyTerminated(std::span<char> field, std::string_view text) -> void {
  Expects(!field.empty(), "a C text field has room for its terminator");
  auto const end = std::ranges::copy(text | std::views::take(field.size() - 1), field.begin()).out;
  std::ranges::fill(end, field.end(), '\0');
}
auto Utf16(std::string_view utf8) -> std::u16string {
  using oxbox::utilities::Encoding;
  auto const bytes = TranscodeRange<std::vector<std::byte>>(
      oxbox::utilities::AsBytes(utf8), { }, { .encoding = Encoding::UTF16, .order = std::endian::native });
  auto const units = oxbox::utilities::SpanCast<char16_t const>(std::span(bytes));
  return { units.begin(), units.end() };
}
}
