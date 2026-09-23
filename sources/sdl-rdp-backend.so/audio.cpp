#include "_detail/state.hpp"
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace Backend {
namespace {
void ApplyVolume(std::span<int16_t> stereo, UINT32 volume)
{
  Expects(stereo.size() % 2 == 0, "stereo frames are complete");
  auto left = int32_t(volume & 0xffff), right = int32_t(volume >> 16);
  for (auto frame : stereo | std::views::chunk(2)) {
    frame[0] = int16_t(int32_t(frame[0]) * left / 65535);
    frame[1] = int16_t(int32_t(frame[1]) * right / 65535);
  }
}
}
bool Peer::SoundChannel()
{
  Expects(channels != nullptr, "channel manager exists");
  if (!active) return true;
  bool healthy = true;
  if (!sound_attempted) {
    sound_attempted = true;
    if (!WTSVirtualChannelManagerIsChannelJoined(channels, RDPSND_CHANNEL_NAME)) return true;
    sound = std::make_unique<AudioChannel>(owner, channels, client->context, wake.get());
    healthy = sound->Initialize();
  }
  if (sound && (!healthy || !sound->Pump())) {
    sound.reset();
    owner.Push({.type = SDLRDP_AUDIO, .audio = {0, 0}});
    owner.audio_changed.notify_all();
  }
  return true;
}
AudioChannel::AudioChannel(State& state, HANDLE channels, rdpContext* context, HANDLE event)
 : owner(state), wake(event), sound(rdpsnd_server_context_new(channels))
{
  Expects(channels && context && event, "sound channel has transport and wake event");
  if (!sound) throw std::runtime_error("Audio channel allocation failed.");
  sound->server_formats = audio_formats_new(2);
  if (!sound->server_formats) throw std::runtime_error("Audio format allocation failed.");
  sound->num_server_formats = 2;
  sound->server_formats[0] = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0, nullptr};
  sound->server_formats[1] = {WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0, nullptr};
  sound->src_format = &sound->server_formats[0];
  sound->data = this;
  sound->rdpcontext = context;
  sound->use_dynamic_virtual_channel = FALSE;
  sound->latency = 10;
  sound->Activated = Activated;
  sound->ConfirmBlock = Confirmed;
}
bool AudioChannel::Initialize()
{
  Expects(sound != nullptr, "sound context exists");
  return sound->Initialize(sound.get(), FALSE) == CHANNEL_RC_OK;
}
bool AudioChannel::Pump()
{
  Expects(sound != nullptr, "sound context exists");
  auto result = rdpsnd_server_handle_messages(sound.get());
  if (result == ERROR_INTERNAL_ERROR && !ready && !rejected && !sound->num_client_formats)
    RejectFormats();
  if (!rejected && (result == CHANNEL_RC_OK || result == ERROR_NO_DATA)) return true;
  sound->Close(sound.get());
  return false;
}
HANDLE AudioChannel::Event() const
{
  Expects(sound != nullptr, "sound context exists");
  return rdpsnd_server_get_event_handle(sound.get());
}
unsigned AudioChannel::Rate() const { return ready ? selected.nSamplesPerSec : 0; }
unsigned AudioChannel::Remaining() const
{
  Expects(ready, "audio has a selected format");
  return selected.nSamplesPerSec / 100 - unsigned(buffer.size() / 2);
}
void AudioChannel::Reset()
{
  sent = confirmed = clock_frames = 0;
  first = clock_start = {};
  server_clock = has_confirmation = false;
  pending.clear();
  buffer.clear();
}
void AudioChannel::Select(unsigned index)
{
  Expects(index < sound->num_client_formats, "client format exists");
  selected = sound->client_formats[index];
  sound->selected_client_format = UINT16(index);
  Reset();
  ready = true;
  owner.audio_changed.notify_all();
}
void AudioChannel::Activated(RdpsndServerContext* context)
{
  Expects(context && context->data, "sound channel has an owner");
  auto& self = *static_cast<AudioChannel*>(context->data);
  static constexpr unsigned wave2_version = 8;
  if (context->clientVersion < wave2_version) {
    self.RejectFormats();
    return;
  }
  if (!context->num_client_formats) { self.RejectFormats(); return; }
  auto const& selected = context->client_formats[0];
  if (selected.wFormatTag != WAVE_FORMAT_PCM || selected.nChannels != 2 || selected.wBitsPerSample != 16
      || (selected.nSamplesPerSec != 48000 && selected.nSamplesPerSec != 44100)) {
    self.RejectFormats();
    return;
  }
  self.Select(0);
  self.owner.Log(SDLRDP_LOG_INFO, std::format("Audio selected: stereo S16 at {} Hz.", selected.nSamplesPerSec));
  self.owner.Push({.type = SDLRDP_AUDIO, .audio = {selected.nSamplesPerSec, 1}});
}
void AudioChannel::RejectFormats()
{
  Expects(sound != nullptr, "sound context exists");
  rejected = true;
  std::string formats;
  for (unsigned i = 0; i < sound->num_client_formats; ++i) {
    auto const& format = sound->client_formats[i];
    formats += std::format("{}tag={} channels={} rate={} bits={}", i ? "; " : "",
      format.wFormatTag, format.nChannels, format.nSamplesPerSec, format.wBitsPerSample);
  }
  owner.Log(SDLRDP_LOG_WARN, std::format("Audio unavailable: client version={}; client formats: {}.",
    sound->clientVersion, formats.empty() ? "none" : formats));
}
void AudioChannel::AdoptServerClock()
{
  Expects(ready, "audio has a selected format");
  auto now = Clock::now();
  // 500 ms permits five default latency windows before abandoning an absent confirmation path.
  if (server_clock || has_confirmation || first == Clock::time_point{}
      || now - first < std::chrono::milliseconds(500)) return;
  server_clock = true;
  clock_start = now;
  clock_frames = confirmed;
  pending.clear();
  owner.Log(SDLRDP_LOG_WARN, "No audio confirmation after 500 ms; using server-clock pacing.");
}
bool AudioChannel::Ready()
{
  Expects(ready, "audio has a selected format");
  auto credit = confirmed;
  if (server_clock) {
    auto now = Clock::now();
    credit = clock_frames
      + uint64_t(std::chrono::duration<double>(now - clock_start).count() * Rate());
    if (credit >= sent) {
      credit = clock_frames = sent;
      clock_start = now;
    }
  }
  if (!buffer.empty()) return true;
  auto unused = std::ranges::find(pending, sound->block_no, &Block::id) == pending.end();
  return unused && (sent <= credit || sent - credit < uint64_t(owner.audio_latency) * Rate() / 1000);
}
UINT AudioChannel::Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp)
{
  Expects(context && context->data, "confirmation has an owner");
  auto& self = *static_cast<AudioChannel*>(context->data);
  auto found = std::ranges::find(self.pending, id, &Block::id);
  if (found == self.pending.end()) return CHANNEL_RC_OK;
  auto rtt = std::chrono::duration<double, std::milli>(Clock::now() - found->sent).count();
  if (!self.has_confirmation) self.owner.Log(SDLRDP_LOG_INFO,
    std::format("Audio block confirm round trip: {:.3f} ms; client timestamp={}; block={}.", rtt, timestamp, id));
  self.confirmed += found->frames;
  self.has_confirmation = true;
  self.pending.erase(found);
  self.owner.audio_changed.notify_all();
  return CHANNEL_RC_OK;
}
bool AudioChannel::Send(std::span<int16_t const> samples)
{
  Expects(ready && samples.size() % 2 == 0 && samples.size() / 2 <= Remaining(), "stereo audio fits pending block");
  buffer.insert(buffer.end(), samples.begin(), samples.end());
  sent += samples.size() / 2;
  if (Remaining()) return true;
  if (sound->capsFlags & TSSNDCAPS_VOLUME) ApplyVolume(buffer, sound->initialVolume);
  auto now = Clock::now();
  auto block = sound->block_no;
  auto timestamp = UINT16(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
  if (sound->SendSamples2(sound.get(), sound->selected_client_format, buffer.data(),
      buffer.size() * sizeof(int16_t), timestamp, 0) != CHANNEL_RC_OK) {
    ready = false;
    owner.Log(SDLRDP_LOG_WARN, "Audio transport ended; discarding samples until reconnection.");
    owner.Push({.type = SDLRDP_AUDIO, .audio = {0, 0}});
    owner.audio_changed.notify_all();
    return false;
  }
  if (first == Clock::time_point{}) first = now;
  if (!server_clock) pending.push_back({block, buffer.size() / 2, now});
  buffer.clear();
  SetEvent(wake);
  return true;
}
void State::OpenAudio()
{
  std::scoped_lock lock(session_guard);
  if (audio_open) throw std::runtime_error("Audio device is already open.");
  EnsurePicture();
  audio_open = true;
}
unsigned State::AudioRate()
{
  std::scoped_lock lock(session_guard);
  return current && current->sound ? current->sound->Rate() : 0;
}
int State::WaitAudio(int timeout)
{
  std::unique_lock lock(session_guard);
  auto deadline = timeout < 0 ? Peer::Clock::time_point::max() : Peer::Clock::now() + std::chrono::milliseconds(timeout);
  for (;;) {
    if (!audio_open || !AudioRate()) return 1;
    current->sound->AdoptServerClock();
    if (current->sound->Ready()) return 1;
    auto now = Peer::Clock::now();
    if (now >= deadline) return 0;
    audio_changed.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(2)));
  }
}
int State::WriteAudio(void const* frames, unsigned count)
{
  Expects(frames || !count, "input covers requested audio frames");
  auto samples = std::span(static_cast<int16_t const*>(frames), std::size_t(count) * 2);
  while (!samples.empty()) {
    WaitAudio(-1);
    std::scoped_lock lock(session_guard);
    if (!audio_open) throw std::runtime_error("Audio device is not open.");
    if (!AudioRate()) return int(count);
    auto& audio = *current->sound;
    if (!audio.Ready()) continue;
    auto size = std::min(samples.size(), std::size_t(audio.Remaining()) * 2);
    if (!audio.Send(samples.first(size))) return int(count);
    samples = samples.subspan(size);
  }
  return int(count);
}
void State::CloseAudio()
{
  std::scoped_lock lock(session_guard);
  audio_open = false;
  if (current && current->sound) current->sound->Reset();
  audio_changed.notify_all();
}
}
