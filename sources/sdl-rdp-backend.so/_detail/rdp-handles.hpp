#pragma once

#include <freerdp/listener.h>
#include <freerdp/peer.h>
#include <winpr/handle.h>

#include <memory>

namespace Backend
{
  template <auto RELEASE>
  struct Releases
  {
    template <typename VTy>
    auto operator()(VTy* what) const -> void { (void)RELEASE(what); }
  };

  struct ReleasesPeer
  {
    auto operator()(freerdp_peer* what) const -> void
    { freerdp_peer_context_free(what); freerdp_peer_free(what); }
  };

  struct ReleasesListener
  {
    void operator()(freerdp_listener* listener) const
    { listener->Close(listener); freerdp_listener_free(listener); }
  };
  using ListenerHandle = std::unique_ptr<freerdp_listener, ReleasesListener>;
  using PeerHandle     = std::unique_ptr<freerdp_peer, ReleasesPeer>;
  using EventHandle    = std::unique_ptr<void, Releases<CloseHandle>>;
}
