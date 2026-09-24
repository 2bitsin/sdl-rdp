#pragma once
#include <sdl-rdp/core/frame-store.hpp>
#include <sdl-rdp/session/peer-set.hpp>

#include <concepts>

namespace Backend {
// The one place the peer lock is taken before the frame lock.
class PeerFrame {
public:
       PeerFrame(PeerSet& peers, FrameStore& frames);
  auto Frame() const noexcept                                            -> FrameLock const&;
  auto ForEach(std::invocable<Peer&, FrameLock const&> auto visit) const -> void {
    _peers.ForEach(_held, [&](Peer& peer) { visit(peer, _frame); });
  }
  auto ReleaseFrame() && noexcept -> FrameLock;

private:
  PeerSet&  _peers;
  PeersLock _held;
  FrameLock _frame;
};
}
