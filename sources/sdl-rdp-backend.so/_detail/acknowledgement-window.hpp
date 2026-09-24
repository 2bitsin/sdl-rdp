#pragma once
#include "sent-frame.hpp"

#include <deque>
#include <vector>

namespace Backend {
// RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
inline constexpr auto     AcknowledgementTimeout  = std::chrono::seconds(1);
inline constexpr unsigned AcknowledgedFrameWindow = 2;
DWORD WaitMilliseconds(std::chrono::nanoseconds remaining, int64_t floor);
class AcknowledgementWindow {
public:
  using Clock = SentFrame::Clock;
  bool                   Enabled() const                noexcept;
  void                   Enable()                       noexcept;
  void                   Disable()                      noexcept;
  void                   Clear()                        noexcept;
  UINT32                 Next()                         noexcept;
  UINT32                 Frame() const                  noexcept;
  void                   Record(uint64_t sequence, Clock::time_point now);
  std::vector<SentFrame> Accept(UINT32 id);
  unsigned               Expire(Clock::time_point now);
  bool                   Open(unsigned window) const    noexcept;
  DWORD                  Remaining(Clock::time_point now) const;
  bool                   Settled(uint64_t target) const noexcept;
  uint64_t               Acknowledged() const           noexcept;

private:
  std::deque<SentFrame> _pending;
  uint64_t              _acknowledged{ };
  UINT32                _frame_id    { };
  bool                  _enabled     { };
};
}
