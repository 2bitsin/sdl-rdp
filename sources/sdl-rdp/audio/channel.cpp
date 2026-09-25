#include <sdl-rdp/audio/channel.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/wtsvc.h>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <tuple>

namespace sdl_rdp::audio::detail::channel {
using sdl_rdp::diagnostics::FailuresThrough;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::freerdp_facade::VirtualChannel;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;

auto FreeSoundContext(RdpsndServerContext* sound) noexcept -> void {
  auto* const channels = sound->vcm;
  rdpsnd_server_context_free(sound);
  // 2bitsin/FreeRDP#1: rdpsnd_main.c:1052 skips the close without an own thread; Open returns the channel.
  auto                 name    = std::to_array(RDPSND_CHANNEL_NAME);
  VirtualChannel const channel { WTSVirtualChannelOpen(channels, WTS_CURRENT_SESSION, name.data()) };
}
namespace {
auto Owner(RdpsndServerContext const& context) -> AudioChannel& {
  return CallbackOwner<AudioChannel, &RdpsndServerContext::data>(context);
}
auto Levels(std::span<std::int16_t const> samples) -> std::string {
  Expects(!samples.empty(), "audio block has samples");
  auto squares = std::ranges::fold_left(
      samples, 0.0, [](double sum, std::int16_t sample) { return sum + (static_cast<double>(sample) * sample); });
  auto peak    = std::ranges::max(samples
                                  | std::views::transform([](std::int16_t sample) { return std::abs(int{ sample }); }));
  return std::format("rms={} peak={}", static_cast<int>(std::sqrt(squares / static_cast<double>(samples.size()))),
                     peak);
}
auto ApplyVolume(std::span<std::int16_t> stereo, std::uint32_t volume) -> void {
  Expects(stereo.size() % 2 == 0, "stereo frames are complete");
  auto left  = Narrowed<std::int32_t>(volume & 0xffff);
  auto right = Narrowed<std::int32_t>(volume >> 16);
  std::ranges::for_each(stereo | std::views::chunk(2), [&](auto frame) {
    frame[0] = Narrowed<std::int16_t>(std::int32_t{ frame[0] } * left / 65535);
    frame[1] = Narrowed<std::int16_t>(std::int32_t{ frame[1] } * right / 65535);
  });
}
constexpr std::uint16_t StereoFrame = 4;
auto StereoPcm(std::uint32_t rate) -> AUDIO_FORMAT {
  return { .wFormatTag      = WAVE_FORMAT_PCM,
           .nChannels       = 2,
           .nSamplesPerSec  = rate,
           .nAvgBytesPerSec = rate * StereoFrame,
           .nBlockAlign     = StereoFrame,
           .wBitsPerSample  = 16,
           .cbSize          = 0,
           .data            = nullptr };
}
auto SoundHandled(std::uint32_t result) -> bool {
  switch (result) {
  case CHANNEL_RC_OK:
  case ERROR_NO_DATA: return true;
  default:            return false;
  }
}
constexpr OperationName AudioActivation  { "Audio activation"         };
constexpr OperationName AudioConfirmation{ "Audio block confirmation" };
using sdl_rdp::freerdp_facade::Handled;
}
class AudioChannel::Callbacks {
public:
  static auto Install(RdpsndServerContext& sound) -> void;
};
auto AudioChannel::Callbacks::Install(RdpsndServerContext& sound) -> void {
  constexpr auto failures = FailuresThrough<&AudioChannel::FailureSource>;
  // abi: psRdpsndServerActivated; psRdpsndServerConfirmBlock, BYTE is uint8_t, UINT16 is uint16_t, UINT is uint32_t
  sound.Activated    = Handled<Owner, &AudioChannel::Activate, AudioActivation, failures>;
  sound.ConfirmBlock = Handled<Owner, &AudioChannel::Confirm, AudioConfirmation, failures, ERROR_INTERNAL_ERROR>;
}
// mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23), so 44.1 kHz is offered first.
AudioChannel::AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                           TraceQueue& traces)
    : _link{ link }, _diagnostics{ diagnostics }, _events{ events }, _session{ session }, _traces{ traces },
      _sound{ rdpsnd_server_context_new(link.Channels()) } {
  if (!_sound) throw AllocationFailed{ "Audio channel" };
  _sound->server_formats = audio_formats_new(2);
  if (!_sound->server_formats) throw AllocationFailed{ "Audio format" };
  _sound->num_server_formats = 2;
  _sound->server_formats[0] = StereoPcm(CompatibleRate);
  _sound->server_formats[1] = StereoPcm(NativeRate);
  _sound->src_format = &_sound->server_formats[0];
  _sound->data = this;
  _sound->rdpcontext = &_link.Context();
  _sound->use_dynamic_virtual_channel = false;
  _sound->latency = 10;
  Callbacks::Install(*_sound);
}
AudioChannel::~AudioChannel() = default;
auto AudioChannel::Initialize() -> bool {
  Expects(_sound != nullptr, "sound context exists");
  return _sound->Initialize(_sound.get(), false) == CHANNEL_RC_OK;
}
auto AudioChannel::Pump() -> bool {
  Expects(_sound != nullptr, "sound context exists");
  auto result = rdpsnd_server_handle_messages(_sound.get());
  if (result == ERROR_INTERNAL_ERROR && !_ready && !_rejected && !_sound->num_client_formats) RejectFormats();
  return !_rejected && SoundHandled(result);
}
auto AudioChannel::Event() const -> WaitHandle {
  Expects(_sound != nullptr, "sound context exists");
  return rdpsnd_server_get_event_handle(_sound.get());
}
auto AudioChannel::Rate() const -> std::uint32_t {
  return _ready ? _rate : 0;
}
auto AudioChannel::Remaining() const -> std::uint32_t {
  Expects(_ready, "audio has a selected format");
  return (_rate / 50) - Narrowed<std::uint32_t>(_buffer.size() / 2);
}
auto AudioChannel::Reset() -> void {
  _sent         = _confirmed = _clock_frames = 0;
  _first        = _clock_start = { };
  _server_clock = _has_confirmation = false;
  _pending.clear();
  _buffer.clear();
}

auto AudioChannel::AdoptServerClock() -> void {
  Expects(_ready, "audio has a selected format");
  auto const now      = Clock::now();
  auto const settling = _first == Clock::time_point{ } || now - _first < std::chrono::milliseconds(500);
  if (_server_clock || _has_confirmation || settling) return;
  _server_clock = true;
  _clock_start  = now;
  _clock_frames = _confirmed;
  _pending.clear();
  _diagnostics.Log(SDLRDP_LOG_WARN, "No audio confirmation after 500 ms; using server-clock pacing.");
}

auto AudioChannel::Send(std::span<std::int16_t const> samples) -> bool {
  Expects(_ready, "channel handshake is complete");
  Expects(samples.size() % 2 == 0, "stereo samples contain complete frames");
  Expects(samples.size() / 2 <= Remaining(), "audio frames fit the pending block");
  _buffer.insert(_buffer.end(), samples.begin(), samples.end());
  _sent += samples.size() / 2;
  if (Remaining()) return true;
  return SendBlock();
}

auto AudioChannel::SendBlock() -> bool {
  if (_sound->capsFlags & TSSNDCAPS_VOLUME) ApplyVolume(_buffer, _sound->initialVolume);
  auto       now          = Clock::now();
  auto       block        = _sound->block_no;
  auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  // MS-RDPEA 2.2.3.3: the wave timestamp is a 16-bit millisecond clock, so it keeps the low 16 bits.
  auto const timestamp = static_cast<std::uint16_t>(milliseconds);
  // SendSamples2 queues PDUs; flush them while the peer may be encoding.
  if (_sound->SendSamples2(_sound.get(), _sound->selected_client_format, _buffer.data(),
                           _buffer.size() * sizeof(std::int16_t), timestamp, 0)
          != CHANNEL_RC_OK
      || !WTSVirtualChannelManagerCheckFileDescriptorEx(_link.Channels(), false)) {
    TransportEnded();
    return false;
  }
  _diagnostics.Line("audio-block", [&] { return std::format("id={} frames={} {}", block, _sent, Levels(_buffer)); });
  RecordBlock(now, block);
  _buffer.clear();
  _link.Signal();
  return true;
}
auto AudioChannel::LogAudio() const -> void {
  Expects(_sound != nullptr, "audio statistics have a channel");
  using Milliseconds = std::chrono::duration<double, std::milli>;
  _diagnostics.Log(
      SDLRDP_LOG_INFO,
      std::format("Audio: {} blocks sent; gap {:.1f} ms mean, {:.1f} ms max; {} gaps over 40 ms.", _blocks_sent,
                  _blocks_sent > 1 ? Milliseconds{ _gap_total }.count() / static_cast<double>(_blocks_sent - 1) : 0,
                  Milliseconds{ _gap_max }.count(), _gaps_over_40ms));
}
auto AudioChannel::FailureSource() const noexcept -> Diagnostics const& {
  return _diagnostics;
}
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
  return static_cast<double>(sent > credit ? sent - credit : 0) * 1000.0 / rate;
}
}
auto AudioChannel::Select(std::size_t index) -> void {
  Expects(index < _sound->num_client_formats, "client format exists");
  _rate                          = _sound->client_formats[index].nSamplesPerSec;
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
auto AudioChannel::Confirm(std::uint8_t id, std::uint16_t timestamp) -> std::uint32_t {
  auto found = std::ranges::find(_pending, id, &Block::id);
  if (found == _pending.end()) return CHANNEL_RC_OK;
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
  return CHANNEL_RC_OK;
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
