#include "_detail/refresh.hpp"
#include <algorithm>
#include <cmath>

namespace Backend {
void Refresh::Restart()
{
  utilities::Expects(ceiling > 0, "declared refresh is positive");
  rate = ceiling;
  average = 1.0 / ceiling;
  last_ack = {};
  last_blocked = {};
  awaiting_empty = 0;
}
void Refresh::Step(Direction direction)
{
  utilities::Expects(ceiling >= 10, "adaptive ceiling reaches the floor");
  switch (direction) {
    case Direction::Down: rate = rate > 20 ? rate - 10 : 10; break;
    case Direction::Hold: break;
    case Direction::Up: rate = std::min(ceiling, rate + 10); break;
    default: utilities::Unreachable(direction);
  }
}
Direction Refresh::FromLatency(Clock::duration latency) const
{
  utilities::Expects(latency >= Clock::duration::zero(), "acknowledgement follows send");
  utilities::Expects(ceiling >= 10, "adaptive ceiling reaches the floor");
  auto seconds = std::chrono::duration<double>(latency).count();
  if (seconds > 2.0 / ceiling) return Direction::Down;
  return seconds < 1.0 / ceiling ? Direction::Up : Direction::Hold;
}
Direction Refresh::FromWire(WireSample const& wire, std::size_t bytes) const
{
  utilities::Expects(bytes > 0, "a frame was written");
  if (!wire.available) return Direction::Hold;
  auto segments = wire.mss ? (bytes + wire.mss - 1) / wire.mss : 0;
  if (wire.outq > bytes || (segments && wire.unacked > segments)) return Direction::Down;
  return wire.outq == 0 ? Direction::Up : Direction::Hold;
}
void Refresh::Blocked(Clock::time_point now)
{
  utilities::Expects(rate > 0, "effective refresh is positive");
  if (mode != RefreshMode::Sender) return;
  awaiting_empty = 0;
  if (last_blocked != Clock::time_point{} && now - last_blocked < std::chrono::duration<double>(1.0 / rate)) return;
  last_blocked = now;
  Step(Direction::Down);
}
void Refresh::Acknowledge(Clock::time_point now, Clock::duration latency)
{
  utilities::Expects(latency >= Clock::duration::zero(), "acknowledgement follows send");
  if (mode == RefreshMode::Client) {
    Step(FromLatency(latency));
  } else if (mode == RefreshMode::Average) {
    if (last_ack != Clock::time_point{}) {
      average = 0.8 * average + 0.2 * std::chrono::duration<double>(now - last_ack).count();
      auto estimate = unsigned(std::clamp(std::round(1.0 / average), 10.0, double(ceiling)));
      if (std::abs(double(estimate) - rate) > rate * 0.05) rate = estimate;
    }
    last_ack = now;
  }
}
void Refresh::Written(WireSample const& wire, std::size_t bytes)
{
  utilities::Expects(bytes > 0, "a frame was written");
  awaiting_empty = 0;
  if (mode != RefreshMode::Sender || !wire.available) return;
  auto direction = FromWire(wire, bytes);
  Step(direction);
  // A write can finish before its tail leaves the kernel queue; observe that completion once.
  if (direction == Direction::Hold) awaiting_empty = bytes;
}
void Refresh::Drained(WireSample const& wire)
{
  utilities::Expects(rate > 0, "effective refresh is positive");
  if (!awaiting_empty || FromWire(wire, awaiting_empty) != Direction::Up) return;
  awaiting_empty = 0;
  Step(Direction::Up);
}
}
