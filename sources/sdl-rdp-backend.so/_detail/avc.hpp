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
IntraRefresh IntraRefreshFor(unsigned fps);
unsigned     Bitrate(Extent size, unsigned kbps = 0);
unsigned     Aligned(unsigned dimension);
void         ReplicateEdges(std::span<BYTE> pixels, Extent size);
struct EncodingTimes {
  std::chrono::nanoseconds convert{ };
  std::chrono::nanoseconds upload { };
  std::chrono::nanoseconds encode { };
};
EncodingTimes& operator += (EncodingTimes& total, EncodingTimes const& frame) noexcept;
} // namespace Backend::Avc
