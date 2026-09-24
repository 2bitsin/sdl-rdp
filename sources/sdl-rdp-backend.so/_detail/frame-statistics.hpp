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
  auto Begin(std::chrono::nanoseconds encoded, uint64_t presents) noexcept -> void;
  auto Sent(FrameCost const& cost, unsigned queued)                        -> void;
  auto Acknowledged(std::chrono::nanoseconds latency)                      -> void;
  auto TimedOut(unsigned count) noexcept                                   -> void;
  auto Summary() const                                                     -> std::string;
  auto Acknowledgements() const noexcept                                   -> uint64_t;

private:
  auto AvcPhases() const -> std::string;
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
