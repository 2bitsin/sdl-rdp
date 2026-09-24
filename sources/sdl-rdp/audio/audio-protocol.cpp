#include <sdl-rdp/audio/audio.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/event-queue.hpp>
#include <sdl-rdp/core/session-access.hpp>
#include <sdl-rdp/core/trace-queue.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>

namespace Backend {
namespace {
auto Supported(RdpsndServerContext const& context) -> bool {
  static constexpr std::uint32_t wave2_version = 8;
  if (context.clientVersion < wave2_version || !context.num_client_formats) return false;
  auto const& selected = context.client_formats[0];
  return selected.wFormatTag == WAVE_FORMAT_PCM && selected.nChannels == 2 && selected.wBitsPerSample == 16
         && (selected.nSamplesPerSec == NativeRate || selected.nSamplesPerSec == CompatibleRate);
}
auto Formats(RdpsndServerContext const& context) -> std::string {
  auto const formats = oxbox::utilities::Joined(
      std::span(context.client_formats, context.num_client_formats), "; ", [](AUDIO_FORMAT const& format) {
        return std::format("tag={} channels={} rate={} bits={}", format.wFormatTag, format.nChannels,
                           format.nSamplesPerSec, format.wBitsPerSample);
      });
  return formats.empty() ? "none" : formats;
}
auto Behind(std::uint64_t sent, std::uint64_t credit, std::uint32_t rate) -> double {
  return double(sent > credit ? sent - credit : 0) * 1000.0 / rate;
}
}
auto AudioChannel::Select(std::size_t index) -> void {
  Expects(index < _sound->num_client_formats, "client format exists");
  _selected                      = _sound->client_formats[index];
  _sound->selected_client_format = Narrowed<std::uint16_t>(index);
  Reset();
  _ready = true;
  _session.AudioChanged();
}
auto AudioChannel::Activate() -> void {
  if (!Supported(*_sound)) {
    RejectFormats();
    return;
  }
  auto const rate = _sound->client_formats[0].nSamplesPerSec;
  Select(0);
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Audio selected: stereo S16 at {} Hz.", rate));
  _events.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = rate, .connected = 1 } });
}
auto AudioChannel::RejectFormats() -> void {
  _rejected = true;
  _diagnostics.Log(SDLRDP_LOG_WARN, std::format("Audio unavailable: client version={}; client formats: {}.",
                                                _sound->clientVersion, Formats(*_sound)));
}
auto AudioChannel::Credit() -> std::uint64_t {
  if (!_server_clock) return _confirmed;
  auto const now    = Clock::now();
  auto const played = std::chrono::duration<double>(now - _clock_start).count() * Rate();
  // Elapsed frames are nonnegative; truncation drops the partial frame.
  auto const credit = _clock_frames + static_cast<std::uint64_t>(played);
  if (credit < _sent) return credit;
  _clock_frames = _sent;
  _clock_start  = now;
  return _sent;
}
auto AudioChannel::ReportGate(bool available, std::uint64_t credit) -> void {
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
auto AudioChannel::Ready(std::uint32_t latency_ms) -> bool {
  Expects(_ready, "audio has a selected format");
  auto const credit = Credit();
  if (!_buffer.empty()) return true;
  auto const unused    = std::ranges::find(_pending, _sound->block_no, &Block::id) == _pending.end();
  auto const allowance = std::uint64_t{ latency_ms } * Rate() / 1000;
  auto const available = unused && (_sent <= credit || _sent - credit < allowance);
  ReportGate(available, credit);
  return available;
}
auto AudioChannel::Confirm(std::uint8_t id, std::uint16_t timestamp) -> void {
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
auto AudioChannel::RecordBlock(Clock::time_point now, std::uint8_t block) -> void {
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
auto AudioChannel::TransportEnded() -> void {
  _ready = false;
  _diagnostics.Log(SDLRDP_LOG_WARN, "Audio transport ended; discarding samples until reconnection.");
  _session.AudioGone();
}
}
