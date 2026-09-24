#pragma once

#include <freerdp/peer.h>
#include <memory>

namespace Backend {
struct ReleasesPeer {
public:
  auto operator()(freerdp_peer* what) const -> void;
};

using PeerHandle = std::unique_ptr<freerdp_peer, ReleasesPeer>;
}
