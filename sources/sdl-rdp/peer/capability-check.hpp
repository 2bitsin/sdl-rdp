#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::peer::detail::capability_check {
using sdl_rdp::auth::Authenticator;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::frame::FramePacing;

class CapabilityCheck : private Pinned {
public:
       CapabilityCheck(PeerLink& link, Authenticator& authenticator, Activation const& activation, FramePacing& pacing,
                       DesktopLayout& desktop, FrameStore& store, Diagnostics const& diagnostics) noexcept;
  auto Accept() -> bool;

private:
  PeerLink&          _link;
  Authenticator&     _authenticator;
  Activation const&  _activation;
  FramePacing&       _pacing;
  DesktopLayout&     _desktop;
  FrameStore&        _store;
  Diagnostics const& _diagnostics;
};
}

namespace sdl_rdp::peer {
using detail::capability_check::CapabilityCheck;
}
