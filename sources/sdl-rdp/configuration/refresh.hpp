#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::configuration::detail::refresh {
inline constexpr std::uint32_t MillihertzPerHz = 1000;
enum class Direction  { Down, Hold, Up                 };
enum class RefreshMode{ Fixed, Client, Average, Sender };
struct WireSample {
  bool          available    { };
  std::uint32_t outq         { };
  std::uint32_t notsent      { };
  std::uint32_t unacked      { };
  std::uint32_t rtt          { };
  std::uint32_t mss          { };
  std::uint64_t delivery_rate{ };
};
class Refresh {
public:
  using Clock = std::chrono::steady_clock;
  explicit    Refresh(RefreshMode selected = RefreshMode::Fixed, std::uint32_t limit = 60);
  auto        Rate() const                                                -> std::uint32_t;
  auto        Mode() const                                                -> RefreshMode;
  auto        AwaitingEmpty() const                                       -> bool;
  auto        Restart()                                                   -> void;
  auto        Step(Direction direction)                                   -> void;
  auto        FromLatency(Clock::duration latency) const                  -> Direction;
  static auto FromWire(WireSample const& wire, std::size_t bytes)         -> Direction;
  auto        Blocked(Clock::time_point now)                              -> void;
  auto        Acknowledge(Clock::time_point now, Clock::duration latency) -> void;
  auto        Written(WireSample const& wire, std::size_t bytes)          -> void;
  auto        Drained(WireSample const& wire)                             -> void;

private:
  auto Average(Clock::time_point now)     -> void;
  auto Estimate(Clock::duration interval) -> void;
  RefreshMode       mode          { RefreshMode::Fixed };
  std::uint32_t     ceiling       { 60                 };
  std::uint32_t     rate          { 60                 };
  double            average       { };
  std::size_t       awaiting_empty{ };
  Clock::time_point last_ack;
  Clock::time_point last_blocked;
};
}

namespace sdl_rdp::configuration {
using detail::refresh::MillihertzPerHz;
using detail::refresh::Refresh;
using detail::refresh::RefreshMode;
using detail::refresh::WireSample;
}
