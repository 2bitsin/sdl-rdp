#include <sdl-rdp/session/audio-output.hpp>

#include <sdl-rdp/audio/audio.hpp>
#include <sdl-rdp/core/configuration.hpp>
#include <sdl-rdp/session/peer.hpp>
#include <sdl-rdp/session/presenter.hpp>

#include <algorithm>
#include <stdexcept>

namespace Backend {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto AudioPollPeriod = std::chrono::milliseconds(2);
}
AudioOutput::AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept
    : _session{ session }, _presenter{ presenter }, _configuration{ configuration } { }
auto AudioOutput::Channel(SessionLock const& held) const -> AudioChannel* {
  auto* const current = _session.Current(held);
  return current ? current->Audio() : nullptr;
}
auto AudioOutput::Open() -> void {
  auto const held = _session.Lock();
  if (_open) throw std::runtime_error("Audio device is already open.");
  _presenter.EnsurePicture();
  _open = true;
}
auto AudioOutput::Rate() -> unsigned {
  auto const  held    = _session.Lock();
  auto const* channel = Channel(held);
  return channel ? channel->Rate() : 0;
}
auto AudioOutput::Wait(int timeout) -> int {
  auto held     = _session.Lock();
  auto deadline = timeout < 0 ? Clock::time_point::max() : Clock::now() + std::chrono::milliseconds(timeout);
  for (;;) {
    if (!_open || !Rate()) return 1;
    auto& channel = *Channel(held);
    channel.AdoptServerClock();
    if (channel.Ready(_configuration.AudioLatency())) return 1;
    auto now = Clock::now();
    if (now >= deadline) return 0;
    _session.WaitAudio(held, std::min(deadline, now + AudioPollPeriod));
  }
}
auto AudioOutput::Write(std::span<int16_t const> samples) -> int {
  auto const count = int(samples.size() / 2);
  while (!samples.empty()) {
    Wait(-1);
    auto const held = _session.Lock();
    if (!_open) throw std::runtime_error("Audio device is not open.");
    if (!Rate()) return count;
    auto& audio = *Channel(held);
    if (!audio.Ready(_configuration.AudioLatency())) continue;
    auto size = std::min(samples.size(), std::size_t(audio.Remaining()) * 2);
    if (!audio.Send(samples.first(size))) return count;
    samples = samples.subspan(size);
  }
  return count;
}
auto AudioOutput::Close() -> void {
  auto const held = _session.Lock();
  _open = false;
  if (auto* channel = Channel(held)) channel->Reset();
  _session.AudioChanged();
}
}
