#pragma once
#include "contract.hpp"

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
                          PeerSet(PeerSet const&)     = delete;
                          PeerSet(PeerSet&&)          = delete;
                          PeerSet()                   = default;
                          ~PeerSet();
  PeerSet&                operator = (PeerSet const&) = delete;
  PeerSet&                operator = (PeerSet&&)      = delete;
  [[nodiscard]] PeersLock Lock();
  void                    ForEach(PeersLock const& held, std::invocable<Peer&> auto visit) {
    Expects(held.mutex() == &_guard, "visiting peers holds the peer lock");
    std::ranges::for_each(_peers, [&visit](auto const& peer) { visit(*peer); });
  }
  void Add(std::unique_ptr<Peer> peer);
  void Reap();

private:
  std::mutex                         _guard;
  std::vector<std::unique_ptr<Peer>> _peers;
};
}
