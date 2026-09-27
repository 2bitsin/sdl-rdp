#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>

namespace sdl_rdp::peer::detail::transport_end {
using sdl_rdp::auth::Authenticator;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::PeerFrames;

class TransportEnd : private Pinned {
public:
       TransportEnd(PeerLink const& link, Activation const& activation, Authenticator& authenticator,
                    PeerFrames const& frames, FrameStore& store, Diagnostics const& diagnostics) noexcept;
  auto Report() -> void;

private:
  auto SecurityEnded() const -> bool;
  auto PendingOutput() const -> bool;
  PeerLink const&    _link;
  Activation const&  _activation;
  Authenticator&     _authenticator;
  PeerFrames const&  _frames;
  FrameStore&        _store;
  Diagnostics const& _diagnostics;
};
}

namespace sdl_rdp::peer {
using detail::transport_end::TransportEnd;
}
