#include "_detail/sent-frame.hpp"

namespace Backend {
SentFrame::SentFrame(UINT32 frame, uint64_t presented, Clock::time_point at) noexcept
    : _id{ frame }, _sequence{ presented }, _sent{ at } { }
auto SentFrame::Id() const noexcept -> UINT32 {
  return _id;
}
auto SentFrame::Sequence() const noexcept -> uint64_t {
  return _sequence;
}
auto SentFrame::Age(Clock::time_point now) const noexcept -> SentFrame::Clock::duration {
  return now - _sent;
}
}
