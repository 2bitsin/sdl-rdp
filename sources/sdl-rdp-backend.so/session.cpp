#include "_detail/session.hpp"

#include "_detail/activation.hpp"
#include "_detail/audio.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer.hpp"

#include <stdexcept>
#include <winpr/synch.h>

namespace Backend {
namespace {
EventHandle ReapSignal() {
  EventHandle signal{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  if (!signal) throw std::runtime_error("peer reaping event allocation failed");
  return signal;
}
void AnnounceDeparture(Session& session, EventQueue& events, Peer const& peer) {
  auto const* sound = peer.Audio();
  events.Push({ .type = SDLRDP_DISCONNECTED });
  if (sound && sound->Rate()) session.AudioGone();
}
}
Session::Session(FrameStore& frames, EventQueue& events)
    : _reap { ReapSignal() }, _frames{ frames }, _events{ events } { }
SessionLock Session::Lock() {
  return SessionLock{ _guard };
}
PeersLock Session::LockPeers() {
  return _peers.Lock();
}
void Session::Add(std::unique_ptr<Peer> peer) {
  _peers.Add(std::move(peer));
}
void Session::Reap() {
  ResetEvent(_reap.get());
  _peers.Reap();
}
HANDLE Session::ReapEvent() const noexcept {
  return _reap.get();
}
FrameLock Session::Takeover(PeerLink const& self) {
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
void Session::Depart(PeerLink const& self, Activation& activation) {
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
Peer* Session::Current(SessionLock const& held) const {
  Expects(held.mutex() == &_guard, "reading the current peer holds the session lock");
  return _current;
}
Peer* Session::Current(FrameLock const& held) const {
  Expects(_frames.Holds(held), "reading the current peer holds the frame lock");
  return _current;
}
unsigned Session::NextDrive() noexcept {
  return _next_drive.fetch_add(1);
}
void Session::AudioChanged() {
  _audio_changed.notify_all();
}
void Session::AudioGone() {
  _events.Push({ .type = SDLRDP_AUDIO, .audio = { .freq = 0, .connected = 0 } });
  AudioChanged();
}
void Session::WaitAudio(SessionLock& held, std::chrono::steady_clock::time_point deadline) {
  Expects(held.mutex() == &_guard, "audio waits hold the session lock");
  _audio_changed.wait_until(held, deadline);
}
}
