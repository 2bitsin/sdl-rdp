#pragma once
#include <chrono>
#include <cstdint>
#include <winpr/wtypes.h>

namespace Backend {
class SentFrame {
public:
  using Clock = std::chrono::steady_clock;
                  SentFrame(UINT32 frame, uint64_t presented, Clock::time_point at) noexcept;
  UINT32          Id() const                                                        noexcept;
  uint64_t        Sequence() const                                                  noexcept;
  Clock::duration Age(Clock::time_point now) const                                  noexcept;

private:
  UINT32            _id;
  uint64_t          _sequence;
  Clock::time_point _sent;
};
}
