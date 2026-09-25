#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

namespace sdl_rdp::video::avc::detail::encoding {
using sdl_rdp::picture::Aligned;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
auto IntraRefreshFor(std::uint32_t fps) -> IntraRefresh {
  Expects(fps, "refresh rate is positive");
  Expects(fps <= UINT32_MAX / 2, "doubled refresh rate fits NVENC");
  // Recovery target: refresh every two seconds, spreading each sweep over half a second.
  IntraRefresh refresh{ .period = 2 * fps, .count = std::max(1u, fps / 2) };
  Ensures(refresh.count <= refresh.period, "refresh sweep fits its period");
  return refresh;
}
auto Bitrate(Extent size, std::uint32_t kbps) -> std::uint32_t {
  Expects(size.width > 0, "surface width is positive");
  Expects(size.height > 0, "surface height is positive");
  Expects(size.width <= 32766, "surface width fits the graphics protocol");
  Expects(size.height <= 32766, "surface height fits the graphics protocol");
  Expects(kbps <= UINT32_MAX / 1000, "bitrate fits NVENC");
  auto const scaled = std::uint64_t{ 16000000 } * size.width * size.height / (1920uz * 1080);
  auto const rate   = kbps ? std::uint64_t{ kbps } * 1000 : std::max(std::uint64_t{ 2000000 }, scaled);
  return Narrowed<std::uint32_t>(std::clamp<std::uint64_t>(rate, 1, UINT32_MAX));
}
auto operator+=(EncodingTimes& total, EncodingTimes const& frame) noexcept -> EncodingTimes& {
  total.convert += frame.convert;
  total.upload  += frame.upload;
  total.encode  += frame.encode;
  return total;
}
auto ReplicateEdges(std::span<std::uint8_t> pixels, Extent size) -> void {
  auto const [width, height] = size;
  auto       stride          = Aligned(width) * 4;
  auto const rows            = Aligned(height);
  Expects(pixels.size() >= std::size_t{ stride } * rows, "picture includes aligned storage");
  std::ranges::for_each(std::views::iota(0u, height), [&](std::uint32_t row) {
    auto line = pixels.subspan(std::size_t{ row } * stride, stride);
    auto edge = line.subspan(std::size_t{ width - 1 } * 4, 4);
    std::ranges::for_each(line.subspan(std::size_t{ width } * 4) | std::views::chunk(4),
                          [&](std::span<std::uint8_t> pixel) { std::ranges::copy(edge, pixel.begin()); });
  });
  auto last = pixels.subspan(std::size_t{ height - 1 } * stride, stride);
  std::ranges::for_each(std::views::iota(height, Aligned(height)), [&](std::uint32_t row) {
    std::ranges::copy(last, pixels.subspan(std::size_t{ row } * stride, stride).begin());
  });
}
}
