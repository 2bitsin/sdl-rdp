#include <sdl-rdp/headless-client.test/utilities/octets.hpp>

#include <sdl-rdp/utilities/transcode.hpp>

#include <oxbox/utilities/span.hpp>
#include <oxbox/utilities/transcode.hpp>
#include <bit>

namespace sdl_rdp::headless_client_test::utilities::detail::octets {
using sdl_rdp::utilities::TranscodeRange;
using sdl_rdp::utilities::Utf16Little;

auto UnicodeText(std::string_view utf8) -> std::vector<std::byte> {
  using oxbox::utilities::Encoding;
  auto text = TranscodeRange<std::vector<std::byte>>(
      oxbox::utilities::AsBytes(utf8), { .encoding = Encoding::UTF8, .order = std::endian::native }, Utf16Little);
  text.resize(text.size() + sizeof(char16_t));
  return text;
}
auto AnsiText(std::string_view text) -> std::vector<std::byte> {
  auto octets = std::vector<std::byte>(std::from_range, oxbox::utilities::AsBytes(text));
  octets.push_back(std::byte{ 0 });
  return octets;
}
}
