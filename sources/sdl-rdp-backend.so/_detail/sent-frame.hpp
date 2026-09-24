#pragma once
#include <winpr/wtypes.h>
#include <chrono>
#include <cstdint>

namespace Backend {
class SentFrame {
public:
  using Clock = std::chrono::steady_clock;
       SentFrame(UINT32 frame, uint64_t presented, Clock::time_point at) noexcept;
  auto Id() const noexcept                       -> UINT32;
  auto Sequence() const noexcept                 -> uint64_t;
  auto Age(Clock::time_point now) const noexcept -> Clock::duration;

private:
  UINT32            _id;
  uint64_t          _sequence;
  Clock::time_point _sent;
};
}
