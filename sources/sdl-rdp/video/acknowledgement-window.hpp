#pragma once
#include <sdl-rdp/video/sent-frame.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace sdl_rdp::video::detail::acknowledgement_window {
// RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
inline constexpr auto        AcknowledgementTimeout  = std::chrono::seconds(1);
inline constexpr std::size_t AcknowledgedFrameWindow = 2;
auto WaitMilliseconds(std::chrono::nanoseconds remaining, std::int64_t floor) -> std::uint32_t;
class AcknowledgementWindow {
public:
  using Clock = SentFrame::Clock;
  auto Enabled() const noexcept                              -> bool;
  auto Enable() noexcept                                     -> void;
  auto Disable() noexcept                                    -> void;
  auto Clear() noexcept                                      -> void;
  auto Next() noexcept                                       -> std::uint32_t;
  auto Frame() const noexcept                                -> std::uint32_t;
  auto Record(std::uint64_t sequence, Clock::time_point now) -> void;
  auto Accept(std::uint32_t id)                              -> std::vector<SentFrame>;
  auto Expire(Clock::time_point now)                         -> std::size_t;
  auto Open(std::size_t window) const noexcept               -> bool;
  auto Remaining(Clock::time_point now) const                -> std::uint32_t;
  auto Settled(std::uint64_t target) const noexcept          -> bool;
  auto Acknowledged() const noexcept                         -> std::uint64_t;

private:
  std::deque<SentFrame> _pending;
  std::uint64_t         _acknowledged{ };
  std::uint32_t         _frame_id    { };
  bool                  _enabled     { };
};
}

namespace sdl_rdp::video {
using detail::acknowledgement_window::AcknowledgedFrameWindow;
using detail::acknowledgement_window::AcknowledgementWindow;
using detail::acknowledgement_window::WaitMilliseconds;
}
