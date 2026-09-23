#pragma once
#include "contract.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Backend {
enum class Direction { Down, Hold, Up };
enum class RefreshMode { Fixed, Client, Average, Sender };
struct WireSample {
  bool available = false;
  unsigned outq = 0, notsent = 0, unacked = 0, rtt = 0, mss = 0;
  uint64_t delivery_rate = 0;
};
WireSample SampleWire(int descriptor);
struct Refresh {
  using Clock = std::chrono::steady_clock;
  RefreshMode mode = RefreshMode::Fixed;
  unsigned ceiling = 60, rate = 60;
  double average = 0;
  std::size_t awaiting_empty = 0;
  Clock::time_point last_ack{}, last_blocked{};
  void Restart();
  void Step(Direction direction);
  Direction FromLatency(Clock::duration latency) const;
  Direction FromWire(WireSample const& wire, std::size_t bytes) const;
  void Blocked(Clock::time_point now);
  void Acknowledge(Clock::time_point now, Clock::duration latency);
  void Written(WireSample const& wire, std::size_t bytes);
  void Drained(WireSample const& wire);
};
}
