#pragma once
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
class Refresh {
public:
  using Clock = std::chrono::steady_clock;
  explicit         Refresh(RefreshMode selected = RefreshMode::Fixed, unsigned limit = 60);
  unsigned         Rate() const;
  RefreshMode      Mode() const;
  bool             AwaitingEmpty() const;
  void             Restart();
  void             Step(Direction direction);
  Direction        FromLatency(Clock::duration latency) const;
  static Direction FromWire(WireSample const& wire, std::size_t bytes);
  void             Blocked(Clock::time_point now);
  auto             Acknowledge(Clock::time_point now, Clock::duration latency) -> void;
  void             Written(WireSample const& wire, std::size_t bytes);
  void             Drained(WireSample const& wire);

private:
  auto Average(Clock::time_point now)     -> void;
  auto Estimate(Clock::duration interval) -> void;
  RefreshMode       mode          { RefreshMode::Fixed };
  unsigned          ceiling       { 60                 };
  unsigned          rate          { 60                 };
  double            average       { };
  std::size_t       awaiting_empty{ };
  Clock::time_point last_ack;
  Clock::time_point last_blocked;
};
}
