#include <sdl-rdp/audio/channel.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/wtsvc.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <tuple>

namespace Backend {
namespace {
auto Owner(RdpsndServerContext* context) -> AudioChannel& {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<AudioChannel>(context->data);
}
auto Levels(std::span<std::int16_t const> samples) -> std::string {
  Expects(!samples.empty(), "audio block has samples");
  auto squares = std::ranges::fold_left(
      samples, 0.0, [](double sum, std::int16_t sample) { return sum + (double(sample) * sample); });
  auto peak    = std::ranges::max(samples
                                  | std::views::transform([](std::int16_t sample) { return std::abs(int(sample)); }));
  return std::format("rms={} peak={}", int(std::sqrt(squares / double(samples.size()))), peak);
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
} // namespace
class AudioChannel::Callbacks {
public:
  static auto Install(RdpsndServerContext& sound) -> void;
};
auto AudioChannel::Callbacks::Install(RdpsndServerContext& sound) -> void {
  // abi: psRdpsndServerActivated
  sound.Activated = [](RdpsndServerContext* context) noexcept {
    auto&      owner    = Owner(context);
    auto const activate = [&] {
      owner.Activate();
      return true;
    };
    std::ignore = Contained(false, activate, FailureLog{ owner._diagnostics, "Audio activation" });
  };
  // abi: psRdpsndServerConfirmBlock, BYTE is uint8_t, UINT16 is uint16_t, UINT is uint32_t
  sound.ConfirmBlock = [](RdpsndServerContext* context, std::uint8_t id,
                          std::uint16_t timestamp) noexcept -> std::uint32_t {
    auto&      owner   = Owner(context);
    auto const confirm = [&]() -> std::uint32_t {
      owner.Confirm(id, timestamp);
      return CHANNEL_RC_OK;
    };
    return Contained(ERROR_INTERNAL_ERROR, confirm, FailureLog{ owner._diagnostics, "Audio block confirmation" });
  };
}
// mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23), so 44.1 kHz is offered first.
AudioChannel::AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                           TraceQueue& traces)
    : _link{ link }, _diagnostics{ diagnostics }, _events{ events }, _session{ session }, _traces{ traces },
      _sound{ rdpsnd_server_context_new(link.Channels()) } {
  if (!_sound) throw std::runtime_error("Audio channel allocation failed.");
  _sound->server_formats = audio_formats_new(2);
  if (!_sound->server_formats) throw std::runtime_error("Audio format allocation failed.");
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
  return _ready ? _selected.nSamplesPerSec : 0;
}
auto AudioChannel::Remaining() const -> std::uint32_t {
  Expects(_ready, "audio has a selected format");
  return (_selected.nSamplesPerSec / 50) - Narrowed<std::uint32_t>(_buffer.size() / 2);
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
  _diagnostics.Log(SDLRDP_LOG_INFO,
                   std::format("Audio: {} blocks sent; gap {:.1f} ms mean, {:.1f} ms max; {} gaps over 40 ms.",
                               _blocks_sent,
                               _blocks_sent > 1 ? Milliseconds(_gap_total).count() / double(_blocks_sent - 1) : 0,
                               Milliseconds(_gap_max).count(), _gaps_over_40ms));
}
} // namespace Backend
