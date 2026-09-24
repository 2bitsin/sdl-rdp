#pragma once
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

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
  auto Next() -> CaptureState;

private:
  auto Begin()                     -> bool;
  auto Take()                      -> sdlrdp_rect;
  auto Resize(sdlrdp_rect picture) -> bool;
  PeerLink&        _link;
  FrameStore&      _store;
  PeerFrames&      _frames;
  DesktopLayout&   _desktop;
  FramePacing&     _pacing;
  FrameStatistics& _statistics;
  Encoder const&   _encoder;
};
}
