#pragma once
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/session/peer-set.hpp>

#include <concepts>
#include <functional>

namespace sdl_rdp::session::detail::peer_frame {
using sdl_rdp::peer::Peer;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameStore;

// The one place the peer lock is taken before the frame lock.
class PeerFrame {
public:
       PeerFrame(PeerSet& peers, FrameStore& frames);
  auto Frame() const noexcept                                            -> FrameLock const&;
  auto ForEach(std::invocable<Peer&, FrameLock const&> auto visit) const -> void {
    _peers.get().ForEach(_held, [&](Peer& peer) { visit(peer, _frame); });
  }
  auto ReleaseFrame() && noexcept -> FrameLock;

private:
  std::reference_wrapper<PeerSet> _peers;
  PeersLock                       _held;
  FrameLock                       _frame;
};
}

namespace sdl_rdp::session {
using detail::peer_frame::PeerFrame;
}
