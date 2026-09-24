#pragma once
#include "pinned.hpp"
#include "sdl-rdp-backend.h"

namespace Backend {
class Activation;
class DesktopLayout;
class Diagnostics;
class FramePacing;
class FrameStore;
class PeerFrames;
class PeerLink;
class SessionAccess;
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
