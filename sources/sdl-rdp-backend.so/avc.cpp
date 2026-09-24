#include "_detail/avc.hpp"
#include "_detail/contract.hpp"

#include <oxbox/utilities/bits.hpp>
#include <algorithm>
#include <cstddef>
#include <ranges>

namespace Backend::Avc {
using utilities::Ensures;
using utilities::Expects;
auto IntraRefreshFor(unsigned fps) -> IntraRefresh {
  Expects(fps, "refresh rate is positive");
  Expects(fps <= UINT32_MAX / 2, "doubled refresh rate fits NVENC");
  // Recovery target: refresh every two seconds, spreading each sweep over half a second.
  IntraRefresh refresh{ .period = 2 * fps, .count = std::max(1u, fps / 2) };
  Ensures(refresh.count <= refresh.period, "refresh sweep fits its period");
  return refresh;
}
auto Aligned(unsigned dimension) -> unsigned {
  Expects(dimension > 0, "surface dimension is positive");
  Expects(dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
auto Bitrate(Extent size, unsigned kbps) -> unsigned {
  Expects(size.width > 0, "surface width is positive");
  Expects(size.height > 0, "surface height is positive");
  Expects(size.width <= 32766, "surface width fits the graphics protocol");
  Expects(size.height <= 32766, "surface height fits the graphics protocol");
  Expects(kbps <= UINT32_MAX / 1000, "bitrate fits NVENC");
  auto const scaled = uint64_t(16000000) * size.width * size.height / (1920uz * 1080);
  auto const rate   = kbps ? uint64_t(kbps) * 1000 : std::max(uint64_t(2000000), scaled);
  return unsigned(std::clamp<uint64_t>(rate, 1, UINT32_MAX));
}
auto operator+=(EncodingTimes& total, EncodingTimes const& frame) noexcept -> EncodingTimes& {
  total.convert += frame.convert;
  total.upload  += frame.upload;
  total.encode  += frame.encode;
  return total;
}
auto ReplicateEdges(std::span<BYTE> pixels, Extent size) -> void {
  auto const [width, height] = size;
  auto       stride          = Aligned(width) * 4;
  Expects(pixels.size() >= std::size_t(stride) * Aligned(height), "picture includes aligned storage");
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned row) {
    auto line = pixels.subspan(std::size_t(row) * stride, stride);
    auto edge = line.subspan(std::size_t(width - 1) * 4, 4);
    std::ranges::for_each(line.subspan(std::size_t(width) * 4) | std::views::chunk(4),
                          [&](auto pixel) { std::ranges::copy(edge, pixel.begin()); });
  });
  auto last = pixels.subspan(std::size_t(height - 1) * stride, stride);
  std::ranges::for_each(std::views::iota(height, Aligned(height)), [&](unsigned row) {
    std::ranges::copy(last, pixels.subspan(std::size_t(row) * stride, stride).begin());
  });
}
}
