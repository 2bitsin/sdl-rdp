#include <sdl-rdp/video/sent-frame.hpp>

namespace Backend {
SentFrame::SentFrame(UINT32 frame, uint64_t presented, Clock::time_point at) noexcept
    : _id{ frame }, _presented{ presented }, _sent{ at } { }
auto SentFrame::Id() const noexcept -> UINT32 {
  return _id;
}
auto SentFrame::Presented() const noexcept -> uint64_t {
  return _presented;
}
auto SentFrame::Age(Clock::time_point now) const noexcept -> SentFrame::Clock::duration {
  return now - _sent;
}
}
