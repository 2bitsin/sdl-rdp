#pragma once
#include <sdl-rdp/utilities/extent.hpp>

#include <chrono>
#include <cstdint>
#include <span>

namespace sdl_rdp::video::avc::detail::encoding {
using sdl_rdp::utilities::Extent;

struct IntraRefresh {
  std::uint32_t period;
  std::uint32_t count;
};
auto IntraRefreshFor(std::uint32_t fps)                          -> IntraRefresh;
auto Bitrate(Extent size, std::uint32_t kbps = 0)                -> std::uint32_t;
auto ReplicateEdges(std::span<std::uint8_t> pixels, Extent size) -> void;
struct EncodingTimes {
  std::chrono::nanoseconds convert{ };
  std::chrono::nanoseconds upload { };
  std::chrono::nanoseconds encode { };
};
auto operator+=(EncodingTimes& total, EncodingTimes const& frame) noexcept -> EncodingTimes&;
}

namespace sdl_rdp::video::avc {
using detail::encoding::Bitrate;
using detail::encoding::EncodingTimes;
using detail::encoding::IntraRefreshFor;
using detail::encoding::ReplicateEdges;
}
