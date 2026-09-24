#pragma once
#include <sdl-rdp/utilities/extent.hpp>

#include <winpr/wtypes.h>
#include <chrono>
#include <span>

namespace Backend::Avc {
struct IntraRefresh {
  unsigned period;
  unsigned count;
};
auto IntraRefreshFor(unsigned fps)                       -> IntraRefresh;
auto Bitrate(Extent size, unsigned kbps = 0)             -> unsigned;
auto ReplicateEdges(std::span<BYTE> pixels, Extent size) -> void;
struct EncodingTimes {
  std::chrono::nanoseconds convert{ };
  std::chrono::nanoseconds upload { };
  std::chrono::nanoseconds encode { };
};
auto operator+=(EncodingTimes& total, EncodingTimes const& frame) noexcept -> EncodingTimes&;
} // namespace Backend::Avc
