#pragma once
#include <chrono>
#include <cstdint>

namespace sdl_rdp::video::detail::sent_frame {
class SentFrame {
public:
  using Clock = std::chrono::steady_clock;
       SentFrame(std::uint32_t frame, std::uint64_t presented, Clock::time_point at) noexcept;
  auto Id() const noexcept                       -> std::uint32_t;
  auto Presented() const noexcept                -> std::uint64_t;
  auto Age(Clock::time_point now) const noexcept -> Clock::duration;

private:
  std::uint32_t     _id;
  std::uint64_t     _presented;
  Clock::time_point _sent;
};
}

namespace sdl_rdp::video {
using detail::sent_frame::SentFrame;
}
