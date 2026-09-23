#include "_detail/state.hpp"
#include <algorithm>
#include <cmath>
#include <freerdp/channels/wtsvc.h>
#include <ranges>
#include <stdexcept>

namespace Backend {
namespace {
std::string Levels(std::span<int16_t const> samples) {
  Expects(!samples.empty(), "audio block has samples");
  double squares = 0;
  int peak = 0;
  for (auto sample : samples) {
    squares += double(sample) * sample;
    peak = std::max(peak, std::abs(int(sample)));
  }
  return std::format("rms={} peak={}", int(std::sqrt(squares / samples.size())), peak);
}
void ApplyVolume(std::span<int16_t> stereo, UINT32 volume) {
  Expects(stereo.size() % 2 == 0, "stereo frames are complete");
  auto left = int32_t(volume & 0xffff);
  auto right = int32_t(volume >> 16);
  for (auto frame : stereo | std::views::chunk(2)) {
    frame[0] = int16_t(int32_t(frame[0]) * left / 65535);
    frame[1] = int16_t(int32_t(frame[1]) * right / 65535);
  }
}
} // namespace
bool Peer::SoundChannel(std::span<HANDLE const> ready) {
  Expects(channels != nullptr, "channel manager exists");
  if (!active)
    return true;
  bool healthy = true;
  if (!sound_attempted) {
    sound_attempted = true;
    if (!WTSVirtualChannelManagerIsChannelJoined(channels, RDPSND_CHANNEL_NAME))
      return true;
    handle_count = 0;
    sound = std::make_unique<AudioChannel>(*this);
    healthy = sound->Initialize();
  }
  if (!sound)
    return true;
  if (healthy && std::ranges::contains(ready, sound->Event()))
    healthy = sound->Pump();
  if (!healthy) {
    handle_count = 0;
    sound.reset();
    owner.Push({.type = SDLRDP_AUDIO, .audio = {.freq = 0, .connected = 0}});
    owner.audio_changed.notify_all();
  }
  return true;
}
AudioChannel::AudioChannel(Peer &peer)
    : peer(peer), owner(peer.owner), channels(peer.channels), sound(rdpsnd_server_context_new(channels)) {
  auto context = peer.client->context;
  Expects(channels != nullptr, "sound channel manager exists");
  Expects(context != nullptr, "sound transport exists");
  if (!sound)
    throw std::runtime_error("Audio channel allocation failed.");
  sound->server_formats = audio_formats_new(2);
  if (!sound->server_formats)
    throw std::runtime_error("Audio format allocation failed.");
  sound->num_server_formats = 2;
  // mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23).
  sound->server_formats[0] = {.wFormatTag = WAVE_FORMAT_PCM, .nChannels = 2, .nSamplesPerSec = 44100, .nAvgBytesPerSec = 176400, .nBlockAlign = 4, .wBitsPerSample = 16, .cbSize = 0, .data = nullptr};
  sound->server_formats[1] = {.wFormatTag = WAVE_FORMAT_PCM, .nChannels = 2, .nSamplesPerSec = 48000, .nAvgBytesPerSec = 192000, .nBlockAlign = 4, .wBitsPerSample = 16, .cbSize = 0, .data = nullptr};
  sound->src_format = &sound->server_formats[0];
  sound->data = this;
  sound->rdpcontext = context;
  sound->use_dynamic_virtual_channel = FALSE;
  sound->latency = 10;
  sound->Activated = Activated;
  sound->ConfirmBlock = Confirmed;
}
AudioChannel::~AudioChannel() {
  sound.reset();
  int major{0};
  int minor{0};
  int revision{0};
  freerdp_get_version(&major, &minor, &revision);
  if (major != 3 || minor != 15 || revision != 0)
    return;
  // FreeRDP 3.15.0 returns the existing static-channel handle from Open.
  auto channel = WTSVirtualChannelOpen(channels, WTS_CURRENT_SESSION, const_cast<char *>(RDPSND_CHANNEL_NAME));
  if (channel)
    WTSVirtualChannelClose(channel);
}
bool AudioChannel::Initialize() {
  Expects(sound != nullptr, "sound context exists");
  return sound->Initialize(sound.get(), FALSE) == CHANNEL_RC_OK;
}
bool AudioChannel::Pump() {
  Expects(sound != nullptr, "sound context exists");
  auto result = rdpsnd_server_handle_messages(sound.get());
  if (result == ERROR_INTERNAL_ERROR && !ready && !rejected && !sound->num_client_formats)
    RejectFormats();
  if (!rejected && (result == CHANNEL_RC_OK || result == ERROR_NO_DATA))
    return true;
  sound->Close(sound.get());
  return false;
}
HANDLE AudioChannel::Event() const {
  Expects(sound != nullptr, "sound context exists");
  return rdpsnd_server_get_event_handle(sound.get());
}
unsigned AudioChannel::Rate() const { return ready ? selected.nSamplesPerSec : 0; }
unsigned AudioChannel::Remaining() const {
  Expects(ready, "audio has a selected format");
  return (selected.nSamplesPerSec / 50) - unsigned(buffer.size() / 2);
}
void AudioChannel::Reset() {
  sent = confirmed = clock_frames = 0;
  first = clock_start = {};
  server_clock = has_confirmation = false;
  pending.clear();
  buffer.clear();
}
void AudioChannel::Select(unsigned index) {
  Expects(index < sound->num_client_formats, "client format exists");
  selected = sound->client_formats[index];
  sound->selected_client_format = UINT16(index);
  Reset();
  ready = true;
  owner.audio_changed.notify_all();
}
void AudioChannel::Activated(RdpsndServerContext *context) {
  Expects(context && context->data, "sound channel has an owner");
  auto &self = *static_cast<AudioChannel *>(context->data);
  static constexpr unsigned wave2_version = 8;
  if (context->clientVersion < wave2_version) {
    self.RejectFormats();
    return;
  }
  if (!context->num_client_formats) {
    self.RejectFormats();
    return;
  }
  auto const &selected = context->client_formats[0];
  if (selected.wFormatTag != WAVE_FORMAT_PCM || selected.nChannels != 2 || selected.wBitsPerSample != 16 || (selected.nSamplesPerSec != 48000 && selected.nSamplesPerSec != 44100)) {
    self.RejectFormats();
    return;
  }
  self.Select(0);
  self.owner.Log(SDLRDP_LOG_INFO, std::format("Audio selected: stereo S16 at {} Hz.", selected.nSamplesPerSec));
  self.owner.Push({.type = SDLRDP_AUDIO, .audio = {.freq = selected.nSamplesPerSec, .connected = 1}});
}
void AudioChannel::RejectFormats() {
  Expects(sound != nullptr, "sound context exists");
  rejected = true;
  std::string formats;
  for (unsigned i = 0; i < sound->num_client_formats; ++i) {
    auto const &format = sound->client_formats[i];
    formats += std::format("{}tag={} channels={} rate={} bits={}", i ? "; " : "",
                           format.wFormatTag, format.nChannels, format.nSamplesPerSec, format.wBitsPerSample);
  }
  owner.Log(SDLRDP_LOG_WARN, std::format("Audio unavailable: client version={}; client formats: {}.",
                                          sound->clientVersion, formats.empty() ? "none" : formats));
}
void AudioChannel::AdoptServerClock() {
  Expects(ready, "audio has a selected format");
  auto now = Clock::now();
  if (server_clock || has_confirmation || first == Clock::time_point{} || now - first < std::chrono::milliseconds(500))
    return;
  server_clock = true;
  clock_start = now;
  clock_frames = confirmed;
  pending.clear();
  owner.Log(SDLRDP_LOG_WARN, "No audio confirmation after 500 ms; using server-clock pacing.");
}
bool AudioChannel::Ready() {
  Expects(ready, "audio has a selected format");
  auto credit = confirmed;
  if (server_clock) {
    auto now = Clock::now();
    credit = clock_frames + uint64_t(std::chrono::duration<double>(now - clock_start).count() * Rate());
    if (credit >= sent) {
      credit = clock_frames = sent;
      clock_start = now;
    }
  }
  if (!buffer.empty())
    return true;
  auto unused = std::ranges::find(pending, sound->block_no, &Block::id) == pending.end();
  auto available = unused && (sent <= credit || sent - credit < uint64_t(owner.audio_latency) * Rate() / 1000);
  if (!available && !gate_warned) {
    gate_warned = true;
    owner.trace.Line("audio-gate", [&] { return std::format("behind={:.1f}", (sent > credit ? sent - credit : 0) * 1000.0 / Rate()); });
    owner.Log(SDLRDP_LOG_WARN, std::format("Audio confirmation gate waiting: client is {:.3f} ms behind.",
                                            (sent > credit ? sent - credit : 0) * 1000.0 / Rate()));
  }
  if (available && gate_warned && owner.trace.Enabled()) {
    gate_warned = false;
    owner.trace.Line("audio-open");
  }
  return available;
}
UINT AudioChannel::Confirmed(RdpsndServerContext *context, BYTE id, UINT16 timestamp) {
  Expects(context && context->data, "confirmation has an owner");
  auto &self = *static_cast<AudioChannel *>(context->data);
  auto found = std::ranges::find(self.pending, id, &Block::id);
  if (found == self.pending.end())
    return CHANNEL_RC_OK;
  auto rtt = std::chrono::duration<double, std::milli>(Clock::now() - found->sent).count();
  if (self.owner.trace.Enabled())
    self.peer.trace_pending.push_back(
        self.owner.trace.Format("audio-confirm", [&] { return std::format("id={} rtt={:.1f}", id, rtt); }));
  if (!self.has_confirmation)
    self.owner.Log(SDLRDP_LOG_INFO,
                    std::format("Audio block confirm round trip: {:.3f} ms; client timestamp={}; block={}.", rtt, timestamp, id));
  self.confirmed += found->frames;
  self.has_confirmation = true;
  self.pending.erase(found);
  self.owner.audio_changed.notify_all();
  return CHANNEL_RC_OK;
}
bool AudioChannel::Send(std::span<int16_t const> samples) {
  Expects(ready && samples.size() % 2 == 0 && samples.size() / 2 <= Remaining(), "stereo audio fits pending block");
  buffer.insert(buffer.end(), samples.begin(), samples.end());
  sent += samples.size() / 2;
  if (Remaining())
    return true;
  if (sound->capsFlags & TSSNDCAPS_VOLUME)
    ApplyVolume(buffer, sound->initialVolume);
  auto now = Clock::now();
  auto block = sound->block_no;
  auto timestamp = UINT16(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
  // SendSamples2 queues PDUs; flush them while the peer may be encoding.
  if (sound->SendSamples2(sound.get(), sound->selected_client_format, buffer.data(),
                          buffer.size() * sizeof(int16_t), timestamp, 0) != CHANNEL_RC_OK ||
      !WTSVirtualChannelManagerCheckFileDescriptorEx(channels, FALSE)) {
    ready = false;
    owner.Log(SDLRDP_LOG_WARN, "Audio transport ended; discarding samples until reconnection.");
    owner.Push({.type = SDLRDP_AUDIO, .audio = {.freq = 0, .connected = 0}});
    owner.audio_changed.notify_all();
    return false;
  }
  owner.trace.Line("audio-block", [&] { return std::format("id={} frames={} {}", block, sent, Levels(buffer)); });
  if (blocks_sent++) {
    auto gap = now - last_send;
    gap_total += gap;
    gap_max = std::max(gap_max, gap);
    if (gap > std::chrono::milliseconds(40))
      ++gaps_over_40ms;
  }
  last_send = now;
  if (first == Clock::time_point{})
    first = now;
  if (!server_clock)
    pending.push_back({block, buffer.size() / 2, now});
  buffer.clear();
  peer.wake.Transition(WakeEvent::Phase::Pending);
  return true;
}
void AudioChannel::LogAudio() {
  Expects(sound != nullptr, "audio statistics have a channel");
  using Milliseconds = std::chrono::duration<double, std::milli>;
  owner.Log(SDLRDP_LOG_INFO, std::format(
                                  "Audio: {} blocks sent; gap {:.1f} ms mean, {:.1f} ms max; {} gaps over 40 ms.",
                                  blocks_sent, blocks_sent > 1 ? Milliseconds(gap_total).count() / (blocks_sent - 1) : 0,
                                  Milliseconds(gap_max).count(), gaps_over_40ms));
}
void State::OpenAudio() {
  std::scoped_lock const lock(session_guard);
  if (audio_open)
    throw std::runtime_error("Audio device is already open.");
  EnsurePicture();
  audio_open = true;
}
unsigned State::AudioRate() {
  std::scoped_lock const lock(session_guard);
  return current && current->sound ? current->sound->Rate() : 0;
}
int State::WaitAudio(int timeout) {
  std::unique_lock lock(session_guard);
  auto deadline = timeout < 0 ? Peer::Clock::time_point::max() : Peer::Clock::now() + std::chrono::milliseconds(timeout);
  for (;;) {
    if (!audio_open || !AudioRate())
      return 1;
    current->sound->AdoptServerClock();
    if (current->sound->Ready())
      return 1;
    auto now = Peer::Clock::now();
    if (now >= deadline)
      return 0;
    audio_changed.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(2)));
  }
}
int State::WriteAudio(void const *frames, unsigned count) {
  Expects(frames || !count, "input covers requested audio frames");
  auto samples = std::span(static_cast<int16_t const *>(frames), std::size_t(count) * 2);
  while (!samples.empty()) {
    WaitAudio(-1);
    std::scoped_lock const lock(session_guard);
    if (!audio_open)
      throw std::runtime_error("Audio device is not open.");
    if (!AudioRate())
      return int(count);
    auto &audio = *current->sound;
    if (!audio.Ready())
      continue;
    auto size = std::min(samples.size(), std::size_t(audio.Remaining()) * 2);
    if (!audio.Send(samples.first(size)))
      return int(count);
    samples = samples.subspan(size);
  }
  return int(count);
}
void State::CloseAudio() {
  std::scoped_lock const lock(session_guard);
  audio_open = false;
  if (current && current->sound)
    current->sound->Reset();
  audio_changed.notify_all();
}
} // namespace Backend
