#include "_detail/session.hpp"

#include "_detail/activation.hpp"
#include "_detail/audio.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer.hpp"

#include <stdexcept>
#include <winpr/synch.h>

namespace Backend {
namespace {
auto ReapSignal() -> EventHandle {
  EventHandle signal{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  if (!signal) throw std::runtime_error("peer reaping event allocation failed");
  return signal;
}
auto AnnounceDeparture(Session& session, EventQueue& events, Peer const& peer) -> void {
  auto const* sound = peer.Audio();
  events.Push({ .type = SDLRDP_DISCONNECTED });
  if (sound && sound->Rate()) session.AudioGone();
}
}
Session::Session(FrameStore& frames, EventQueue& events)
    : _reap { ReapSignal() }, _frames{ frames }, _events{ events } { }
auto Session::Lock() -> SessionLock {
  return SessionLock{ _guard };
}
auto Session::LockPeers() -> PeersLock {
  return _peers.Lock();
}
auto Session::Add(std::unique_ptr<Peer> peer) -> void {
  _peers.Add(std::move(peer));
}
auto Session::Reap() -> void {
  ResetEvent(_reap.get());
  _peers.Reap();
}
auto Session::ReapEvent() const noexcept -> HANDLE {
  return _reap.get();
}
auto Session::Takeover(PeerLink const& self) -> FrameLock {
  std::scoped_lock const session(_guard);
  auto const             held    = LockPeers();
  auto                   frame   = _frames.Lock();
  ForEach(held, [&](Peer& peer) {
    if (peer.Owns(self))
      _current = &peer;
    else if (peer.Evict())
      AnnounceDeparture(*this, _events, peer);
  });
  Ensures(_current != nullptr, "the arriving peer is current");
  return frame;
}
auto Session::Depart(PeerLink const& self, Activation& activation) -> void {
  {
    std::scoped_lock const session(_guard);
    auto const             held    = LockPeers();
    auto const             frame   = _frames.Lock();
    ForEach(held, [&](Peer& peer) {
      if (!peer.Owns(self)) return;
      if (_current == &peer) _current = nullptr;
      if (activation.Deactivate()) AnnounceDeparture(*this, _events, peer);
    });
  }
  _frames.Notify();
  AudioChanged();
  activation.Finish();
  SetEvent(_reap.get());
}
auto Session::Current(SessionLock const& held) const -> Peer* {
  Expects(held.mutex() == &_guard, "reading the current peer holds the session lock");
  return _current;
}
auto Session::Current(FrameLock const& held) const -> Peer* {
  Expects(_frames.Holds(held), "reading the current peer holds the frame lock");
  return _current;
}
auto Session::NextDrive() noexcept -> unsigned {
  return _next_drive.fetch_add(1);
}
auto Session::AudioChanged() -> void {
  _audio_changed.notify_all();
}
auto Session::AudioGone() -> void {
  _events.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = 0, .connected = 0 } });
  AudioChanged();
}
auto Session::WaitAudio(SessionLock& held, std::chrono::steady_clock::time_point deadline) -> void {
  Expects(held.mutex() == &_guard, "audio waits hold the session lock");
  _audio_changed.wait_until(held, deadline);
}
}
