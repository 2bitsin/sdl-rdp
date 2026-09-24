#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Backend {
Refresh::Refresh(RefreshMode selected, std::uint32_t limit) : mode(selected), ceiling(limit) {
  utilities::Expects(limit > 0, "refresh ceiling is positive");
}
auto Refresh::Rate() const -> std::uint32_t {
  return rate;
}
auto Refresh::Mode() const -> RefreshMode {
  return mode;
}
auto Refresh::AwaitingEmpty() const -> bool {
  return awaiting_empty != 0;
}
auto Refresh::Restart() -> void {
  utilities::Expects(ceiling > 0, "declared refresh is positive");
  rate           = ceiling;
  average        = 1.0 / ceiling;
  last_ack       = { };
  last_blocked   = { };
  awaiting_empty = 0;
}
auto Refresh::Step(Direction direction) -> void {
  utilities::Expects(ceiling >= 10, "adaptive ceiling reaches the floor");
  switch (direction) {
  case Direction::Down: rate = rate > 20 ? rate - 10 : 10; break;
  case Direction::Hold: break;
  case Direction::Up:   rate = std::min(ceiling, rate + 10); break;
  default:              utilities::Unreachable(direction);
  }
}
auto Refresh::FromLatency(Clock::duration latency) const -> Direction {
  utilities::Expects(latency >= Clock::duration::zero(), "acknowledgement follows send");
  utilities::Expects(ceiling >= 10, "adaptive ceiling reaches the floor");
  auto seconds = std::chrono::duration<double>(latency).count();
  if (seconds > 2.0 / ceiling) return Direction::Down;
  return seconds < 1.0 / ceiling ? Direction::Up : Direction::Hold;
}
auto Refresh::FromWire(WireSample const& wire, std::size_t bytes) -> Direction {
  utilities::Expects(bytes > 0, "a frame was written");
  if (!wire.available) return Direction::Hold;
  auto segments = wire.mss ? (bytes + wire.mss - 1) / wire.mss : 0;
  if (wire.outq > bytes || (segments && wire.unacked > segments)) return Direction::Down;
  return wire.outq == 0 ? Direction::Up : Direction::Hold;
}
auto Refresh::Blocked(Clock::time_point now) -> void {
  utilities::Expects(rate > 0, "effective refresh is positive");
  if (mode != RefreshMode::Sender) return;
  awaiting_empty = 0;
  if (last_blocked != Clock::time_point{ } && now - last_blocked < std::chrono::duration<double>(1.0 / rate)) return;
  last_blocked = now;
  Step(Direction::Down);
}
auto Refresh::Acknowledge(Clock::time_point now, Clock::duration latency) -> void {
  utilities::Expects(latency >= Clock::duration::zero(), "acknowledgement follows send");
  if (mode == RefreshMode::Client) Step(FromLatency(latency));
  if (mode == RefreshMode::Average) Average(now);
}
auto Refresh::Average(Clock::time_point now) -> void {
  if (last_ack != Clock::time_point{ }) Estimate(now - last_ack);
  last_ack = now;
}
auto Refresh::Estimate(Clock::duration interval) -> void {
  average = 0.8 * average + 0.2 * std::chrono::duration<double>(interval).count();
  auto estimate = Narrowed<std::uint32_t>(std::lround(std::clamp(1.0 / average, 10.0, double(ceiling))));
  if (std::abs(double(estimate) - rate) > rate * 0.05) rate = estimate;
}
auto Refresh::Written(WireSample const& wire, std::size_t bytes) -> void {
  utilities::Expects(bytes > 0, "a frame was written");
  awaiting_empty = 0;
  if (mode != RefreshMode::Sender || !wire.available) return;
  auto direction = FromWire(wire, bytes);
  Step(direction);
  // A write can finish before its tail leaves the kernel queue; observe that completion once.
  if (direction == Direction::Hold) awaiting_empty = bytes;
}
auto Refresh::Drained(WireSample const& wire) -> void {
  utilities::Expects(rate > 0, "effective refresh is positive");
  if (!awaiting_empty || FromWire(wire, awaiting_empty) != Direction::Up) return;
  awaiting_empty = 0;
  Step(Direction::Up);
}
}
