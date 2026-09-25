#include <sdl-rdp/video/frame/statistics.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <concepts>
#include <cstddef>
#include <format>

namespace sdl_rdp::video::frame::detail::statistics {
using sdl_rdp::utilities::Expects;

namespace {
using Milliseconds = std::chrono::duration<double, std::milli>;
constexpr auto SlowAcknowledgement = std::chrono::milliseconds(100);
auto Mean(std::convertible_to<double> auto total, std::uint64_t count) -> double {
  return count ? static_cast<double>(total) / static_cast<double>(count) : 0;
}
auto MeanMilliseconds(std::chrono::nanoseconds total, std::uint64_t count) -> double {
  return Mean(Milliseconds{ total }.count(), count);
}
}
auto FrameStatistics::Begin(std::chrono::nanoseconds encoded, std::uint64_t presents) noexcept -> void {
  _coalesced += presents ? presents - 1 : 0;
  _started   =  encoded;
}
auto FrameStatistics::Sent(FrameCost const& cost, std::size_t queued) -> void {
  _outq.Add(queued);
  _encode.Add(cost.encoded - _started);
  if (!cost.avc) return;
  ++_avc_frames;
  _avc += *cost.avc;
}
auto FrameStatistics::Acknowledged(std::chrono::nanoseconds latency) -> void {
  Expects(latency >= std::chrono::nanoseconds::zero(), "acknowledgement follows frame send");
  _acknowledgement.Add(latency);
  if (latency > SlowAcknowledgement) ++_slow;
}
auto FrameStatistics::TimedOut(std::size_t count) noexcept -> void {
  _timed_out += count;
}
auto FrameStatistics::AvcPhases() const -> std::string {
  if (!_avc_frames) return { };
  return std::format(" (convert {:.1f}, upload {:.1f}, nvenc {:.1f})", MeanMilliseconds(_avc.convert, _avc_frames),
                     MeanMilliseconds(_avc.upload, _avc_frames), MeanMilliseconds(_avc.encode, _avc_frames));
}
auto FrameStatistics::Summary() const -> std::string {
  return std::format("Frames: {} sent, {} coalesced; encode {:.1f} ms mean, {:.1f} ms max{}; acknowledgement {:.1f} ms "
                     "mean, {:.1f} ms max, {} over 100 ms, {} timed out. Send buffer: {:.1f} bytes mean, {} bytes max.",
                     _encode.Count(), _coalesced, MeanMilliseconds(_encode.Total(), _encode.Count()),
                     Milliseconds{ _encode.Maximum() }.count(), AvcPhases(),
                     MeanMilliseconds(_acknowledgement.Total(), _acknowledgement.Count()),
                     Milliseconds{ _acknowledgement.Maximum() }.count(), _slow, _timed_out,
                     Mean(_outq.Total(), _encode.Count()), _outq.Maximum());
}
auto FrameStatistics::Acknowledgements() const noexcept -> std::uint64_t {
  return _acknowledgement.Count();
}
}
