#pragma once
#include <functional>
#include <oxbox/utilities/transcode.hpp>
#include <stdexcept>
#include <vector>

namespace Backend {
template <class Output, class Map = std::identity>
Output TranscodeRange(std::span<std::byte const> input, oxbox::utilities::TextFormat source,
                      oxbox::utilities::TextFormat target, Map map = { }) {
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
  auto data = reinterpret_cast<typename Output::value_type const*>(encoded.data());
  return Output(data, data + encoded.size());
}
}
