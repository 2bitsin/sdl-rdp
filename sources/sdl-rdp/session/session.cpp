#include <sdl-rdp/session/session.hpp>

#include <sdl-rdp/audio/channel.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/peer/peer.hpp>

#include <winpr/synch.h>
#include <cstdint>
#include <utility>

namespace sdl_rdp::session::detail::session {
using sdl_rdp::freerdp_facade::ManualResetEvent;
using sdl_rdp::link::Disconnected;
using sdl_rdp::peer::Peer;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;

namespace {
auto ReapSignal() -> EventHandle {
  return ManualResetEvent("Peer reaping event");
}
auto AnnounceDeparture(Session& session, EventQueue& events, Peer const& peer) -> void {
  auto const sound = peer.Redirected().Audio();
  events.Push(Disconnected{ });
  if (sound && sound->get().Rate()) session.AudioGone();
}
}
Session::Session(FrameStore& frames, EventQueue& events)
    : _reap{ ReapSignal() }, _frames{ frames }, _events{ events } { }
auto Session::Lock() -> SessionLock {
  return SessionLock{ _guard };
}
auto Session::LockPeersAndFrame() -> PeerFrame {
  return { _peers, _frames };
}
auto Session::Add(std::unique_ptr<Peer> peer) -> void {
  _peers.Add(std::move(peer));
}
auto Session::Reap() -> void {
  ResetEvent(_reap.get());
  _peers.Reap();
}
auto Session::ReapEvent() const -> WaitHandle {
  return WaitHandle{ _reap };
}
auto Session::Takeover(PeerLink const& self) -> FrameLock {
  std::scoped_lock const session(_guard);
  auto                   peers   = LockPeersAndFrame();
  peers.ForEach([&](Peer& peer, FrameLock const& /*held*/) {
    if (peer.Owns(self))
      _current = std::ref(peer);
    else if (peer.Evict())
      AnnounceDeparture(*this, _events, peer);
  });
  Ensures(_current.has_value(), "the arriving peer is current");
  return std::move(peers).ReleaseFrame();
}
auto Session::Depart(PeerLink const& self, Activation& activation) -> void {
  {
    std::scoped_lock const session(_guard);
    LockPeersAndFrame().ForEach([&](Peer& peer, FrameLock const& /*held*/) {
      if (!peer.Owns(self)) return;
      if (_current && &_current->get() == &peer) _current.reset();
      if (activation.Deactivate()) AnnounceDeparture(*this, _events, peer);
    });
  }
  _frames.Notify();
  AudioChanged();
  activation.Finish();
  SetEvent(_reap.get());
}
auto Session::Current(SessionLock const& held) const -> CurrentPeer {
  Expects(held.mutex() == &_guard, "reading the current peer holds the session lock");
  return _current;
}
auto Session::Current(FrameLock const& held) const -> CurrentPeer {
  Expects(_frames.Holds(held), "reading the current peer holds the frame lock");
  return _current;
}
auto Session::NextDrive() noexcept -> std::uint32_t {
  return _next_drive.fetch_add(1);
}
auto Session::AudioChanged() -> void {
  _audio_changed.notify_all();
}
auto Session::AudioGone() -> void {
  _events.Push(sdl_rdp::link::AudioChanged{ });
  AudioChanged();
}
auto Session::WaitAudio(SessionLock& held, std::chrono::steady_clock::time_point deadline) -> void {
  Expects(held.mutex() == &_guard, "audio waits hold the session lock");
  _audio_changed.wait_until(held, deadline);
}
}
