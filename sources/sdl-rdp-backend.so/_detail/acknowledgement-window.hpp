#pragma once
#include "sent-frame.hpp"

#include <deque>
#include <vector>

namespace Backend {
// RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
inline constexpr auto     AcknowledgementTimeout  = std::chrono::seconds(1);
inline constexpr unsigned AcknowledgedFrameWindow = 2;
auto WaitMilliseconds(std::chrono::nanoseconds remaining, int64_t floor) -> DWORD;
class AcknowledgementWindow {
public:
  using Clock = SentFrame::Clock;
  auto Enabled() const noexcept                         -> bool;
  auto Enable() noexcept                                -> void;
  auto Disable() noexcept                               -> void;
  auto Clear() noexcept                                 -> void;
  auto Next() noexcept                                  -> UINT32;
  auto Frame() const noexcept                           -> UINT32;
  auto Record(uint64_t sequence, Clock::time_point now) -> void;
  auto Accept(UINT32 id)                                -> std::vector<SentFrame>;
  auto Expire(Clock::time_point now)                    -> unsigned;
  auto Open(unsigned window) const noexcept             -> bool;
  auto Remaining(Clock::time_point now) const           -> DWORD;
  auto Settled(uint64_t target) const noexcept          -> bool;
  auto Acknowledged() const noexcept                    -> uint64_t;

private:
  std::deque<SentFrame> _pending;
  uint64_t              _acknowledged{ };
  UINT32                _frame_id    { };
  bool                  _enabled     { };
};
}
