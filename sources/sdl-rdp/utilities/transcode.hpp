#pragma once
#include <oxbox/utilities/span.hpp>
#include <oxbox/utilities/transcode.hpp>
#include <bit>
#include <functional>
#include <stdexcept>
#include <vector>

namespace Backend {
inline constexpr oxbox::utilities::TextFormat Utf16Little{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                           .order    = std::endian::little };
template <class Output, class Map = std::identity>
auto TranscodeRange(std::span<std::byte const> input, oxbox::utilities::TextFormat source,
                    oxbox::utilities::TextFormat target, Map map = { }) -> Output {
  using namespace oxbox::utilities;
  static_assert(sizeof(typename Output::value_type) == sizeof(std::byte));
  std::vector<std::byte> encoded;
  while (!input.empty()) {
    auto point = DecodeFromBytes<char32_t>(input, source.encoding, source.order);
    if (!point) throw std::runtime_error("Invalid text encoding.");
    auto size = encoded.size();
    EncodeAppend(map(*point), std::back_inserter(encoded), target);
    if (encoded.size() == size) throw std::runtime_error("Unrepresentable codepoint.");
  }
  if (encoded.empty()) return { };
  auto const text = SpanCast<typename Output::value_type const>(std::span(encoded));
  return Output(text.begin(), text.end());
}
}
