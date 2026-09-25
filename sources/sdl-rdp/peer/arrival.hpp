#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::peer::detail::arrival {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::PeerFrames;
using sdl_rdp::video::frame::FramePacing;

class Arrival : private Pinned {
public:
       Arrival(SessionAccess& session, FrameStore& store, PeerFrames& frames, PeerLink& link, Activation& activation,
               DesktopLayout& desktop, FramePacing& pacing, Diagnostics const& diagnostics) noexcept;
  auto Admit(sdlrdp_codec codec) -> void;

private:
  auto Connection(sdlrdp_codec codec) const                      -> sdlrdp_event;
  auto Enter(sdlrdp_event const& connection, sdlrdp_codec codec) -> void;
  SessionAccess&     _session;
  FrameStore&        _store;
  PeerFrames&        _frames;
  PeerLink&          _link;
  Activation&        _activation;
  DesktopLayout&     _desktop;
  FramePacing&       _pacing;
  Diagnostics const& _diagnostics;
};
}

namespace sdl_rdp::peer {
using detail::arrival::Arrival;
}
