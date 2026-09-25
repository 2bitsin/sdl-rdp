#pragma once
#include <sdl-rdp/utilities/running-statistics.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace sdl_rdp::video::frame::detail::statistics {
using sdl_rdp::utilities::RunningStatistics;
using sdl_rdp::video::avc::EncodingTimes;

struct FrameCost {
  std::size_t                  bytes  { };
  std::chrono::nanoseconds     encoded{ };
  std::optional<EncodingTimes> avc;
};
class FrameStatistics {
public:
  auto Begin(std::chrono::nanoseconds encoded, std::uint64_t presents) noexcept -> void;
  auto Sent(FrameCost const& cost, std::size_t queued)                          -> void;
  auto Acknowledged(std::chrono::nanoseconds latency)                           -> void;
  auto TimedOut(std::size_t count) noexcept                                     -> void;
  auto Summary() const                                                          -> std::string;
  auto Acknowledgements() const noexcept                                        -> std::uint64_t;

private:
  auto AvcPhases() const -> std::string;
  RunningStatistics<std::chrono::nanoseconds> _encode;
  RunningStatistics<std::chrono::nanoseconds> _acknowledgement;
  RunningStatistics<std::uint64_t>            _outq;
  EncodingTimes                               _avc;
  std::uint64_t                               _avc_frames     { };
  std::uint64_t                               _coalesced      { };
  std::uint64_t                               _slow           { };
  std::uint64_t                               _timed_out      { };
  std::chrono::nanoseconds                    _started        { };
};
}

namespace sdl_rdp::video::frame {
using detail::statistics::FrameCost;
using detail::statistics::FrameStatistics;
}
