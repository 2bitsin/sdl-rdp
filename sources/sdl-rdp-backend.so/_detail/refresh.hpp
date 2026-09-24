#pragma once
#include "contract.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Backend {
inline constexpr unsigned MillihertzPerHz = 1000;
enum class Direction  { Down, Hold, Up                 };
enum class RefreshMode{ Fixed, Client, Average, Sender };
struct WireSample {
  bool     available    { };
  unsigned outq         { };
  unsigned notsent      { };
  unsigned unacked      { };
  unsigned rtt          { };
  unsigned mss          { };
  uint64_t delivery_rate{ };
};
WireSample SampleWire(int descriptor);
struct Refresh {
  using Clock = std::chrono::steady_clock;
  explicit Refresh(RefreshMode selected = RefreshMode::Fixed, unsigned limit = 60) : mode(selected), ceiling(limit) {
    utilities::Expects(limit > 0, "refresh ceiling is positive");
  }
  unsigned         Rate() const { return rate; }
  RefreshMode      Mode() const { return mode; }
  bool             AwaitingEmpty() const { return awaiting_empty != 0; }
  void             Restart();
  void             Step(Direction direction);
  Direction        FromLatency(Clock::duration latency) const;
  static Direction FromWire(WireSample const& wire, std::size_t bytes);
  void             Blocked(Clock::time_point now);
  void             Acknowledge(Clock::time_point now, Clock::duration latency);
  void             Written(WireSample const& wire, std::size_t bytes);
  void             Drained(WireSample const& wire);

private:
  RefreshMode       mode          { RefreshMode::Fixed };
  unsigned          ceiling       { 60                 };
  unsigned          rate          { 60                 };
  double            average       { };
  std::size_t       awaiting_empty{ };
  Clock::time_point last_ack;
  Clock::time_point last_blocked;
};
}
