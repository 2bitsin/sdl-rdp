#pragma once

#include <freerdp/listener.h>
#include <freerdp/peer.h>
#include <memory>
#include <winpr/handle.h>
#include <winpr/stream.h>

namespace Backend {
template <auto RELEASE> struct Releases {
public:
  template <typename VTy> auto operator()(VTy* what) const -> void { (void)RELEASE(what); }
};

struct ReleaseStream {
public:
  void operator()(wStream* stream) const { Stream_Free(stream, TRUE); }
};

struct ReleasesPeer {
public:
  auto operator()(freerdp_peer* what) const -> void {
    freerdp_peer_context_free(what);
    freerdp_peer_free(what);
  }
};

struct ReleasesListener {
public:
  void operator()(freerdp_listener* listener) const {
    listener->Close(listener);
    freerdp_listener_free(listener);
  }
};
using ListenerHandle = std::unique_ptr<freerdp_listener, ReleasesListener>;
using PeerHandle     = std::unique_ptr<freerdp_peer, ReleasesPeer>;
using EventHandle    = std::unique_ptr<void, Releases<CloseHandle>>;
}
