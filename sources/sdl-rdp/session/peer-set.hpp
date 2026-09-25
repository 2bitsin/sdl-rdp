#pragma once
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <concepts>
#include <memory>
#include <mutex>
#include <vector>

namespace sdl_rdp::session::detail::peer_set {
using sdl_rdp::peer::Peer;
using sdl_rdp::utilities::Expects;

using PeersLock = std::unique_lock<std::mutex>;
class PeerSet {
public:
                     PeerSet(PeerSet const&)                                                      = delete;
                     PeerSet(PeerSet&&)                                                           = delete;
                     PeerSet()                                                                    = default;
                     ~PeerSet();
  auto               operator=(PeerSet const&)                                        -> PeerSet& = delete;
  auto               operator=(PeerSet&&)                                             -> PeerSet& = delete;
  [[nodiscard]] auto Lock()                                                           -> PeersLock;
  auto               ForEach(PeersLock const& held, std::invocable<Peer&> auto visit) -> void {
    Expects(held.mutex() == &_guard, "visiting peers holds the peer lock");
    std::ranges::for_each(_peers, [&visit](auto const& peer) { visit(*peer); });
  }
  auto Add(std::unique_ptr<Peer> peer) -> void;
  auto Reap()                          -> void;

private:
  std::mutex                         _guard;
  std::vector<std::unique_ptr<Peer>> _peers;
};
}

namespace sdl_rdp::session {
using detail::peer_set::PeerSet;
using detail::peer_set::PeersLock;
}
