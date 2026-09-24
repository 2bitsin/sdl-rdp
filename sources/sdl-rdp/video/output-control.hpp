#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>

namespace Backend {
class Activation;
class FramePacing;
class GraphicsLink;
class PeerFrames;
class PeerLink;
class OutputControl : private Pinned {
public:
       OutputControl(PeerLink& link, GraphicsLink const& graphics, FramePacing& pacing, Activation& activation,
                     PeerFrames& frames) noexcept;
  auto Acknowledge(UINT32 id) -> void;
  auto Suppress(bool allow)   -> void;

private:
  PeerLink&           _link;
  GraphicsLink const& _graphics;
  FramePacing&        _pacing;
  Activation&         _activation;
  PeerFrames&         _frames;
};
}
