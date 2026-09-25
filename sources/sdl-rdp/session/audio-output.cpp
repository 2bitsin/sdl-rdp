#include <sdl-rdp/session/audio-output.hpp>

#include <sdl-rdp/audio/channel.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/exceptions.hpp>
#include <sdl-rdp/session/presenter.hpp>
#include <sdl-rdp/utilities/deadline.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Backend {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto AudioPollPeriod = std::chrono::milliseconds(2);
}
AudioOutput::AudioOutput(Session& session, Presenter& presenter, Configuration const& configuration) noexcept
    : _session{ session }, _presenter{ presenter }, _configuration{ configuration } { }
auto AudioOutput::Channel(SessionLock const& held) const -> AudioChannel* {
  auto* const current = _session.Current(held);
  return current ? current->Redirected().Audio() : nullptr;
}
auto AudioOutput::Open() -> void {
  auto const held = _session.Lock();
  if (_open) throw AudioDeviceState{ "already open" };
  _presenter.EnsurePicture();
  _open = true;
}
auto AudioOutput::Rate() -> std::uint32_t {
  auto const  held    = _session.Lock();
  auto const* channel = Channel(held);
  return channel ? channel->Rate() : 0;
}
auto AudioOutput::Wait(Deadline deadline) -> int {
  auto held = _session.Lock();
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
auto AudioOutput::Write(std::span<std::int16_t const> samples) -> int {
  auto const count = int(samples.size() / 2);
  while (!samples.empty()) {
    Wait(Deadline::max());
    auto const held = _session.Lock();
    if (!_open) throw AudioDeviceState{ "not open" };
    if (!Rate()) return count;
    auto& audio = *Channel(held);
    if (!audio.Ready(_configuration.AudioLatency())) continue;
    auto size = std::min(samples.size(), std::size_t{ audio.Remaining() } * 2);
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
