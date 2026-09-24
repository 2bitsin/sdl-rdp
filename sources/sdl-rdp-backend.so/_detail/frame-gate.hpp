#pragma once
#include "pinned.hpp"

namespace Backend {
class Activation;
class DesktopLayout;
class FramePacing;
class FrameStore;
class GraphicsLink;
class PeerFrames;
class PeerLink;
class FrameGate : private Pinned {
public:
       FrameGate(PeerLink& link, FrameStore& store, PeerFrames& frames, DesktopLayout& desktop, FramePacing& pacing,
                 Activation const& activation, GraphicsLink const& graphics) noexcept;
  auto Admit() -> bool;

private:
  auto Settle() -> bool;
  PeerLink&           _link;
  FrameStore&         _store;
  PeerFrames&         _frames;
  DesktopLayout&      _desktop;
  FramePacing&        _pacing;
  Activation const&   _activation;
  GraphicsLink const& _graphics;
};
}
