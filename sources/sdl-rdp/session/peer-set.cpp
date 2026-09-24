#include <sdl-rdp/session/peer-set.hpp>

#include <sdl-rdp/session/peer.hpp>

#include <utility>

namespace Backend {
PeerSet::~PeerSet() {
  // Declared before the lock, so the peers join after it is released.
  std::vector<std::unique_ptr<Peer>> retired;
  auto const                         held    = Lock();
  ForEach(held, [](Peer& peer) { peer.Stop(); });
  retired = std::exchange(_peers, { });
}
auto PeerSet::Lock() -> PeersLock {
  return PeersLock{ _guard };
}
auto PeerSet::Add(std::unique_ptr<Peer> peer) -> void {
  Expects(peer != nullptr, "an accepted peer exists");
  std::scoped_lock const lock(_guard);
  _peers.push_back(std::move(peer));
  _peers.back()->Start();
}
auto PeerSet::Reap() -> void {
  std::scoped_lock const lock(_guard);
  std::erase_if(_peers, [](auto const& peer) { return peer->Finished(); });
}
}
