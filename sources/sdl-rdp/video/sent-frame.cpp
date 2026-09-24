#include <sdl-rdp/video/sent-frame.hpp>
#include <cstdint>

namespace Backend {
SentFrame::SentFrame(std::uint32_t frame, std::uint64_t presented, Clock::time_point at) noexcept
    : _id{ frame }, _presented{ presented }, _sent{ at } { }
auto SentFrame::Id() const noexcept -> std::uint32_t {
  return _id;
}
auto SentFrame::Presented() const noexcept -> std::uint64_t {
  return _presented;
}
auto SentFrame::Age(Clock::time_point now) const noexcept -> SentFrame::Clock::duration {
  return now - _sent;
}
}
