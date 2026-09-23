#include "_detail/state.hpp"

#include <algorithm>

namespace Backend {
void AudioProtocol::Select(AudioChannel& self, unsigned index) {
  Expects(index < self.sound->num_client_formats, "client format exists");
  self.selected                      = self.sound->client_formats[index];
  self.sound->selected_client_format = UINT16(index);
  self.Reset();
  self.ready = true;
  self.owner.audio_changed.notify_all();
}
bool AudioProtocol::Supported(RdpsndServerContext const& context) {
  static constexpr unsigned wave2_version = 8;
  if (context.clientVersion < wave2_version || !context.num_client_formats) return false;
  auto const& selected = context.client_formats[0];
  return selected.wFormatTag == WAVE_FORMAT_PCM && selected.nChannels == 2 && selected.wBitsPerSample == 16 &&
         (selected.nSamplesPerSec == 48000 || selected.nSamplesPerSec == 44100);
}
void AudioProtocol::Activated(RdpsndServerContext* context) {
  Expects(context, "callback context exists");
  Expects(context->data, "channel context carries its owner");
  auto& self = *static_cast<AudioChannel*>(context->data);
  if (!Supported(*context)) {
    RejectFormats(self);
    return;
  }
  auto const& selected = context->client_formats[0];
  Select(self, 0);
  self.owner.Log(SDLRDP_LOG_INFO, std::format("Audio selected: stereo S16 at {} Hz.", selected.nSamplesPerSec));
  self.owner.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = selected.nSamplesPerSec, .connected = 1 } });
}
void AudioProtocol::RejectFormats(AudioChannel& self) {
  Expects(self.sound != nullptr, "sound context exists");
  self.rejected = true;
  std::string formats;
  for (unsigned i = 0; i < self.sound->num_client_formats; ++i) {
    auto const& format = self.sound->client_formats[i];
    formats += std::format("{}tag={} channels={} rate={} bits={}", i ? "; " : "", format.wFormatTag, format.nChannels,
                           format.nSamplesPerSec, format.wBitsPerSample);
  }
  self.owner.Log(SDLRDP_LOG_WARN, std::format("Audio unavailable: client version={}; client formats: {}.",
                                              self.sound->clientVersion, formats.empty() ? "none" : formats));
}
uint64_t AudioProtocol::Credit(AudioChannel& self) {
  auto credit = self.confirmed;
  if (self.server_clock) {
    auto now = AudioChannel::Clock::now();
    credit = self.clock_frames + uint64_t(std::chrono::duration<double>(now - self.clock_start).count() * self.Rate());
    if (credit >= self.sent) {
      credit = self.clock_frames = self.sent;
      self.clock_start           = now;
    }
  }
  return credit;
}
void AudioProtocol::ReportGate(AudioChannel& self, bool available, uint64_t credit) {
  if (!available && !self.gate_warned) {
    self.gate_warned = true;
    self.owner.trace.Line("audio-gate", [&] {
      return std::format("behind={:.1f}", double(self.sent > credit ? self.sent - credit : 0) * 1000.0 / self.Rate());
    });
    self.owner.Log(SDLRDP_LOG_WARN,
                   std::format("Audio confirmation gate waiting: client is {:.3f} ms behind.",
                               double(self.sent > credit ? self.sent - credit : 0) * 1000.0 / self.Rate()));
  }
  if (available && self.gate_warned && self.owner.trace.Enabled()) {
    self.gate_warned = false;
    self.owner.trace.Line("audio-open");
  }
}
bool AudioProtocol::Ready(AudioChannel& self) {
  Expects(self.ready, "audio has a selected format");
  auto credit = Credit(self);
  if (!self.buffer.empty()) return true;
  auto unused = std::ranges::find(self.pending, self.sound->block_no, &AudioChannel::Block::id) == self.pending.end();
  auto available =
      unused && (self.sent <= credit || self.sent - credit < uint64_t(self.owner.audio_latency) * self.Rate() / 1000);
  ReportGate(self, available, credit);
  return available;
}
UINT AudioProtocol::Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp) {
  Expects(context, "callback context exists");
  Expects(context->data, "channel context carries its owner");
  auto& self = *static_cast<AudioChannel*>(context->data);
  auto found = std::ranges::find(self.pending, id, &AudioChannel::Block::id);
  if (found == self.pending.end()) return CHANNEL_RC_OK;
  auto rtt = std::chrono::duration<double, std::milli>(AudioChannel::Clock::now() - found->sent).count();
  if (self.owner.trace.Enabled())
    self.peer.trace_pending.push_back(
        self.owner.trace.Format("audio-confirm", [&] { return std::format("id={} rtt={:.1f}", id, rtt); }));
  if (!self.has_confirmation)
    self.owner.Log(
        SDLRDP_LOG_INFO,
        std::format("Audio block confirm round trip: {:.3f} ms; client timestamp={}; block={}.", rtt, timestamp, id));
  self.confirmed += found->frames;
  self.has_confirmation = true;
  self.pending.erase(found);
  self.owner.audio_changed.notify_all();
  return CHANNEL_RC_OK;
}
void AudioProtocol::RecordBlock(AudioChannel& self, AudioChannel::Clock::time_point now, BYTE block) {
  if (self.blocks_sent++) {
    auto gap = now - self.last_send;
    self.gap_total += gap;
    self.gap_max = std::max(self.gap_max, gap);
    if (gap > std::chrono::milliseconds(40)) ++self.gaps_over_40ms;
  }
  self.last_send = now;
  if (self.first == AudioChannel::Clock::time_point{}) self.first = now;
  if (!self.server_clock) self.pending.push_back({ block, self.buffer.size() / 2, now });
}
void AudioProtocol::TransportEnded(AudioChannel& self) {
  self.ready = false;
  self.owner.Log(SDLRDP_LOG_WARN, "Audio transport ended; discarding samples until reconnection.");
  self.owner.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = 0, .connected = 0 } });
  self.owner.audio_changed.notify_all();
}

}
