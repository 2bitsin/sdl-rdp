#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/span.hpp>
#include <oxbox/utilities/transcode.hpp>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::utilities::detail::transcode {
inline constexpr oxbox::utilities::TextFormat Utf16Little{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                           .order    = std::endian::little };
template <class Output, class Map = std::identity>
auto TranscodeRange(std::span<std::byte const> input, oxbox::utilities::TextFormat source,
                    oxbox::utilities::TextFormat target, Map map = { }) -> Output {
  using oxbox::utilities::DecodeFromBytes;
  using oxbox::utilities::EncodeAppend;
  using oxbox::utilities::SpanCast;
  static_assert(sizeof(typename Output::value_type) == sizeof(std::byte));
  std::vector<std::byte> encoded;
  while (!input.empty()) {
    auto point = DecodeFromBytes<char32_t>(input, source.encoding, source.order);
    if (!point) throw InvalidEncoding{ };
    auto const mapped = map(*point);
    auto const size   = encoded.size();
    EncodeAppend(mapped, std::back_inserter(encoded), target);
    if (encoded.size() == size) throw Unencodable{ mapped };
  }
  if (encoded.empty()) return { };
  auto const text = SpanCast<typename Output::value_type const>(std::span(encoded));
  return Output(text.begin(), text.end());
}
auto Utf16(std::string_view utf8) -> std::u16string;
}

namespace sdl_rdp::utilities {
using detail::transcode::TranscodeRange;
using detail::transcode::Utf16;
using detail::transcode::Utf16Little;
}
