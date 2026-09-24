#pragma once
#include "extent.hpp"

#include <chrono>
#include <span>
#include <winpr/wtypes.h>

namespace Backend::Avc {
struct IntraRefresh {
  unsigned period;
  unsigned count;
};
auto IntraRefreshFor(unsigned fps)                       -> IntraRefresh;
auto Bitrate(Extent size, unsigned kbps = 0)             -> unsigned;
auto Aligned(unsigned dimension)                         -> unsigned;
auto ReplicateEdges(std::span<BYTE> pixels, Extent size) -> void;
struct EncodingTimes {
  std::chrono::nanoseconds convert{ };
  std::chrono::nanoseconds upload { };
  std::chrono::nanoseconds encode { };
};
auto operator += (EncodingTimes& total, EncodingTimes const& frame) noexcept -> EncodingTimes&;
} // namespace Backend::Avc
