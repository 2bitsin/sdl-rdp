#include "_detail/frame-statistics.hpp"

#include "_detail/contract.hpp"

#include <concepts>
#include <format>

namespace Backend {
namespace {
using Milliseconds = std::chrono::duration<double, std::milli>;
constexpr auto SlowAcknowledgement = std::chrono::milliseconds(100);
double Mean(std::convertible_to<double> auto total, uint64_t count) {
  return count ? double(total) / double(count) : 0;
}
double MeanMilliseconds(std::chrono::nanoseconds total, uint64_t count) {
  return Mean(Milliseconds(total).count(), count);
}
}
void FrameStatistics::Begin(std::chrono::nanoseconds encoded, uint64_t presents) noexcept {
  _coalesced += presents ? presents - 1 : 0;
  _started   =  encoded;
}
void FrameStatistics::Sent(FrameCost const& cost, unsigned queued) {
  _outq.Add(queued);
  _encode.Add(cost.encoded - _started);
  if (!cost.avc) return;
  ++_avc_frames;
  _avc += *cost.avc;
}
void FrameStatistics::Acknowledged(std::chrono::nanoseconds latency) {
  Expects(latency >= std::chrono::nanoseconds::zero(), "acknowledgement follows frame send");
  _acknowledgement.Add(latency);
  if (latency > SlowAcknowledgement) ++_slow;
}
void FrameStatistics::TimedOut(unsigned count) noexcept {
  _timed_out += count;
}
std::string FrameStatistics::AvcPhases() const {
  if (!_avc_frames) return { };
  return std::format(" (convert {:.1f}, upload {:.1f}, nvenc {:.1f})", MeanMilliseconds(_avc.convert, _avc_frames),
                     MeanMilliseconds(_avc.upload, _avc_frames), MeanMilliseconds(_avc.encode, _avc_frames));
}
std::string FrameStatistics::Summary() const {
  return std::format(
      "Frames: {} sent, {} coalesced; encode {:.1f} ms mean, {:.1f} ms max{}; acknowledgement {:.1f} ms "
      "mean, {:.1f} ms max, {} over 100 ms, {} timed out. Send buffer: {:.1f} bytes mean, {} bytes max.",
      _encode.Count(), _coalesced, MeanMilliseconds(_encode.Total(), _encode.Count()),
      Milliseconds(_encode.Maximum()).count(), AvcPhases(),
      MeanMilliseconds(_acknowledgement.Total(), _acknowledgement.Count()),
      Milliseconds(_acknowledgement.Maximum()).count(), _slow, _timed_out, Mean(_outq.Total(), _encode.Count()),
      _outq.Maximum());
}
uint64_t FrameStatistics::Acknowledgements() const noexcept {
  return _acknowledgement.Count();
}
}
