#include "_detail/audio.hpp"
#include "_detail/callback-owner.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/session-access.hpp"
#include "_detail/trace-queue.hpp"

#include <algorithm>
#include <format>

namespace Backend {
namespace {
AudioChannel& Owner(RdpsndServerContext* context) {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<AudioChannel>(context->data);
}
bool Supported(RdpsndServerContext const& context) {
  static constexpr unsigned wave2_version = 8;
  if (context.clientVersion < wave2_version || !context.num_client_formats) return false;
  auto const& selected = context.client_formats[0];
  return selected.wFormatTag == WAVE_FORMAT_PCM && selected.nChannels == 2 && selected.wBitsPerSample == 16 &&
         (selected.nSamplesPerSec == NativeRate || selected.nSamplesPerSec == CompatibleRate);
}
std::string Formats(RdpsndServerContext const& context) {
  std::string formats;
  for (auto const& format : std::span(context.client_formats, context.num_client_formats))
    formats += std::format("{}tag={} channels={} rate={} bits={}", formats.empty() ? "" : "; ", format.wFormatTag,
                           format.nChannels, format.nSamplesPerSec, format.wBitsPerSample);
  return formats.empty() ? "none" : formats;
}
double Behind(uint64_t sent, uint64_t credit, unsigned rate) {
  return double(sent > credit ? sent - credit : 0) * 1000.0 / rate;
}
}
void AudioChannel::Select(unsigned index) {
  Expects(index < _sound->num_client_formats, "client format exists");
  _selected                      = _sound->client_formats[index];
  _sound->selected_client_format = UINT16(index);
  Reset();
  _ready = true;
  _session.AudioChanged();
}
void AudioChannel::Activated(RdpsndServerContext* context) {
  auto& self = Owner(context);
  if (!Supported(*context)) {
    self.RejectFormats();
    return;
  }
  auto const rate = context->client_formats[0].nSamplesPerSec;
  self.Select(0);
  self._diagnostics.Log(SDLRDP_LOG_INFO, std::format("Audio selected: stereo S16 at {} Hz.", rate));
  self._events.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = rate, .connected = 1 } });
}
void AudioChannel::RejectFormats() {
  _rejected = true;
  _diagnostics.Log(SDLRDP_LOG_WARN, std::format("Audio unavailable: client version={}; client formats: {}.",
                                                _sound->clientVersion, Formats(*_sound)));
}
uint64_t AudioChannel::Credit() {
  if (!_server_clock) return _confirmed;
  auto const now    = Clock::now();
  auto const credit = _clock_frames + uint64_t(std::chrono::duration<double>(now - _clock_start).count() * Rate());
  if (credit < _sent) return credit;
  _clock_frames = _sent;
  _clock_start  = now;
  return _sent;
}
void AudioChannel::ReportGate(bool available, uint64_t credit) {
  if (!available && !_gate_warned) {
    _gate_warned = true;
    _diagnostics.Line("audio-gate", [&] { return std::format("behind={:.1f}", Behind(_sent, credit, Rate())); });
    _diagnostics.Log(SDLRDP_LOG_WARN, std::format("Audio confirmation gate waiting: client is {:.3f} ms behind.",
                                                  Behind(_sent, credit, Rate())));
  }
  if (available && _gate_warned && _diagnostics.Tracing()) {
    _gate_warned = false;
    _diagnostics.Line("audio-open");
  }
}
bool AudioChannel::Ready(unsigned latency_ms) {
  Expects(_ready, "audio has a selected format");
  auto const credit = Credit();
  if (!_buffer.empty()) return true;
  auto const unused    = std::ranges::find(_pending, _sound->block_no, &Block::id) == _pending.end();
  auto const allowance = uint64_t(latency_ms) * Rate() / 1000;
  auto const available = unused && (_sent <= credit || _sent - credit < allowance);
  ReportGate(available, credit);
  return available;
}
UINT AudioChannel::Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp) {
  Owner(context).Confirm(id, timestamp);
  return CHANNEL_RC_OK;
}
void AudioChannel::Confirm(BYTE id, UINT16 timestamp) {
  auto found = std::ranges::find(_pending, id, &Block::id);
  if (found == _pending.end()) return;
  auto rtt = std::chrono::duration<double, std::milli>(Clock::now() - found->sent).count();
  _traces.Defer("audio-confirm", [&] { return std::format("id={} rtt={:.1f}", id, rtt); });
  if (!_has_confirmation)
    _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Audio block confirm round trip: {:.3f} ms; client timestamp={}; "
                                                  "block={}.",
                                                  rtt, timestamp, id));
  _confirmed        += found->frames;
  _has_confirmation =  true;
  _pending.erase(found);
  _session.AudioChanged();
}
void AudioChannel::RecordBlock(Clock::time_point now, BYTE block) {
  if (_blocks_sent++) {
    auto gap = now - _last_send;
    _gap_total += gap;
    _gap_max   =  std::max(_gap_max, gap);
    if (gap > std::chrono::milliseconds(40)) ++_gaps_over_40ms;
  }
  _last_send = now;
  if (_first == Clock::time_point{ }) _first = now;
  if (!_server_clock) _pending.push_back({ block, _buffer.size() / 2, now });
}
void AudioChannel::TransportEnded() {
  _ready = false;
  _diagnostics.Log(SDLRDP_LOG_WARN, "Audio transport ended; discarding samples until reconnection.");
  _session.AudioGone();
}
}
