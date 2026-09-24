#pragma once
#include "avc.hpp"
#include "running-statistics.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace Backend {
struct FrameCost {
  std::size_t                       bytes  { };
  std::chrono::nanoseconds          encoded{ };
  std::optional<Avc::EncodingTimes> avc;
};
class FrameStatistics {
public:
  void        Begin(std::chrono::nanoseconds encoded, uint64_t presents) noexcept;
  void        Sent(FrameCost const& cost, unsigned queued);
  void        Acknowledged(std::chrono::nanoseconds latency);
  void        TimedOut(unsigned count)                                   noexcept;
  std::string Summary() const;
  uint64_t    Acknowledgements() const                                   noexcept;

private:
  std::string AvcPhases() const;
  RunningStatistics<std::chrono::nanoseconds> _encode;
  RunningStatistics<std::chrono::nanoseconds> _acknowledgement;
  RunningStatistics<uint64_t>                 _outq;
  Avc::EncodingTimes                          _avc;
  uint64_t                                    _avc_frames     { };
  uint64_t                                    _coalesced      { };
  uint64_t                                    _slow           { };
  uint64_t                                    _timed_out      { };
  std::chrono::nanoseconds                    _started        { };
};
}
