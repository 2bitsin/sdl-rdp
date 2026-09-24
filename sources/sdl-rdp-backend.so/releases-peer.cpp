#include "_detail/releases-peer.hpp"

namespace Backend {
auto ReleasesPeer::operator()(freerdp_peer* what) const -> void {
  freerdp_peer_context_free(what);
  freerdp_peer_free(what);
}
}
