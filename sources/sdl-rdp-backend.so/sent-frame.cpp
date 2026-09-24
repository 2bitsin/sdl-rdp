#include "_detail/sent-frame.hpp"

namespace Backend {
SentFrame::SentFrame(UINT32 frame, uint64_t presented, Clock::time_point at) noexcept
    : _id { frame }, _sequence{ presented }, _sent{ at } { }
UINT32 SentFrame::Id() const noexcept {
  return _id;
}
uint64_t SentFrame::Sequence() const noexcept {
  return _sequence;
}
SentFrame::Clock::duration SentFrame::Age(Clock::time_point now) const noexcept {
  return now - _sent;
}
}
