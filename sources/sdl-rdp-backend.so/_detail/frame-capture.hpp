#pragma once
#include "pinned.hpp"
#include "sdl-rdp-backend.h"

namespace Backend {
class DesktopLayout;
class Encoder;
class FramePacing;
class FrameStatistics;
class FrameStore;
class PeerFrames;
class PeerLink;
enum class CaptureState{ Failed, Idle, Captured };
class FrameCapture : private Pinned {
public:
  FrameCapture(PeerLink& link, FrameStore& store, PeerFrames& frames, DesktopLayout& desktop, FramePacing& pacing,
               FrameStatistics& statistics, Encoder const& encoder) noexcept;
  CaptureState Next();

private:
  bool        Begin();
  sdlrdp_rect Take();
  bool        Resize(sdlrdp_rect picture);
  PeerLink&        _link;
  FrameStore&      _store;
  PeerFrames&      _frames;
  DesktopLayout&   _desktop;
  FramePacing&     _pacing;
  FrameStatistics& _statistics;
  Encoder const&   _encoder;
};
}
