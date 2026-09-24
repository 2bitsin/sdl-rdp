#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <concepts>
#include <memory>
#include <mutex>
#include <vector>

namespace Backend {
class Peer;
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
