#include <sdl-rdp/audio/channel.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/chunk.hpp>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <tuple>

namespace sdl_rdp::audio::detail::channel {
using oxbox::utilities::Chunk;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::AudioFormat;
using sdl_rdp::freerdp_facade::SoundPump;
using sdl_rdp::freerdp_facade::WavePcm;
using sdl_rdp::link::AudioChanged;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

namespace {
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
  std::ranges::for_each(stereo | Chunk(2), [&](std::span<std::int16_t> frame) {
    frame[0] = Narrowed<std::int16_t>(std::int32_t{ frame[0] } * left / 65535);
    frame[1] = Narrowed<std::int16_t>(std::int32_t{ frame[1] } * right / 65535);
  });
}
constexpr auto StereoPcm(std::uint32_t rate) -> AudioFormat {
  return { .tag = WavePcm, .channels = 2, .rate = rate, .bits = 16 };
}
// mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23), so 44.1 kHz is offered first.
constexpr std::array                Offered{ StereoPcm(CompatibleRate), StereoPcm(NativeRate) };
constexpr std::chrono::milliseconds Latency{ 10                                               };
}
AudioChannel::AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                           TraceQueue& traces)
    : LoggedFailures{ diagnostics }, _link{ link }, _events{ events }, _session{ session }, _traces{ traces },
      _sound{ link.Channels(), *this, Offered, Latency } { }
auto AudioChannel::Initialize() -> bool {
  return _sound.Initialize();
}
auto AudioChannel::Pump() -> bool {
  auto const result = _sound.Pump();
  if (result == SoundPump::FailedBeforeFormats && !_ready && !_rejected) RejectFormats(_sound.Client());
  return !_rejected && result == SoundPump::Handled;
}
auto AudioChannel::Event() const -> WaitHandle {
  return _sound.Handle();
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
  Logger().Log(LogLevel::Warn, "No audio confirmation after 500 ms; using server-clock pacing.");
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
  if (auto const volume = _sound.Volume()) ApplyVolume(_buffer, *volume);
  auto       now          = Clock::now();
  auto       block        = _sound.NextBlock();
  auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  // MS-RDPEA 2.2.3.3: the wave timestamp is a 16-bit millisecond clock, so it keeps the low 16 bits.
  auto const timestamp = static_cast<std::uint16_t>(milliseconds);
  // The channel queues the samples PDUs; flush them while the peer may be encoding.
  if (!_sound.SendSamples(_buffer, timestamp) || !_link.Channels().Flush()) {
    TransportEnded();
    return false;
  }
  Logger().Line("audio-block", [&] { return std::format("id={} frames={} {}", block, _sent, Levels(_buffer)); });
  RecordBlock(now, block);
  _buffer.clear();
  _link.Signal();
  return true;
}
auto AudioChannel::LogAudio() const -> void {
  using Milliseconds = std::chrono::duration<double, std::milli>;
  Logger().Log(
      LogLevel::Info,
      std::format("Audio: {} blocks sent; gap {:.1f} ms mean, {:.1f} ms max; {} gaps over 40 ms.", _blocks_sent,
                  _blocks_sent > 1 ? Milliseconds{ _gap_total }.count() / static_cast<double>(_blocks_sent - 1) : 0,
                  Milliseconds{ _gap_max }.count(), _gaps_over_40ms));
}
namespace {
auto Supported(SoundClient const& client) -> bool {
  static constexpr std::uint32_t wave2_version = 8;
  if (client.version < wave2_version || client.formats.empty()) return false;
  auto const& selected = client.formats.front();
  return selected.tag == WavePcm && selected.channels == 2 && selected.bits == 16
         && (selected.rate == NativeRate || selected.rate == CompatibleRate);
}
auto Formats(SoundClient const& client) -> std::string {
  auto const formats = oxbox::utilities::Joined(client.formats, "; ", [](AudioFormat const& format) {
    return std::format("tag={} channels={} rate={} bits={}", format.tag, format.channels, format.rate, format.bits);
  });
  return formats.empty() ? "none" : formats;
}
auto Behind(std::uint64_t sent, std::uint64_t credit, std::uint32_t rate) -> double {
  return static_cast<double>(sent > credit ? sent - credit : 0) * 1000.0 / rate;
}
}
auto AudioChannel::Select(std::size_t index) -> void {
  _rate = _sound.Select(index).rate;
  Reset();
  _ready = true;
  _session.AudioChanged();
}
auto AudioChannel::Activated(SoundClient const& client) -> void {
  if (!Supported(client)) {
    RejectFormats(client);
    return;
  }
  auto const rate = client.formats.front().rate;
  Select(0);
  Logger().Log(LogLevel::Info, std::format("Audio selected: stereo S16 at {} Hz.", rate));
  _events.Push(AudioChanged{ .rate = rate, .connected = true });
}
auto AudioChannel::RejectFormats(SoundClient const& client) -> void {
  _rejected = true;
  Logger().Log(LogLevel::Warn, std::format("Audio unavailable: client version={}; client formats: {}.", client.version,
                                           Formats(client)));
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
    Logger().Line("audio-gate", [&] { return std::format("behind={:.1f}", Behind(_sent, credit, Rate())); });
    Logger().Log(LogLevel::Warn, std::format("Audio confirmation gate waiting: client is {:.3f} ms behind.",
                                             Behind(_sent, credit, Rate())));
  }
  if (available && _gate_warned && Logger().Tracing()) {
    _gate_warned = false;
    Logger().Line("audio-open");
  }
}
auto AudioChannel::Ready(std::uint32_t latency_ms) -> bool {
  Expects(_ready, "audio has a selected format");
  auto const credit = Credit();
  if (!_buffer.empty()) return true;
  auto const unused    = std::ranges::find(_pending, _sound.NextBlock(), &Block::id) == _pending.end();
  auto const allowance = std::uint64_t{ latency_ms } * Rate() / 1000;
  auto const available = unused && (_sent <= credit || _sent - credit < allowance);
  ReportGate(available, credit);
  return available;
}
auto AudioChannel::Confirmed(BlockConfirm confirm) -> void {
  auto const [id, timestamp] = confirm;
  auto       found           = std::ranges::find(_pending, id, &Block::id);
  if (found == _pending.end()) return;
  auto rtt = std::chrono::duration<double, std::milli>(Clock::now() - found->sent).count();
  _traces.Defer("audio-confirm", [&] { return std::format("id={} rtt={:.1f}", id, rtt); });
  if (!_has_confirmation)
    Logger().Log(LogLevel::Info, std::format("Audio block confirm round trip: {:.3f} ms; client timestamp={}; "
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
  Logger().Log(LogLevel::Warn, "Audio transport ended; discarding samples until reconnection.");
  _session.AudioGone();
}
}
