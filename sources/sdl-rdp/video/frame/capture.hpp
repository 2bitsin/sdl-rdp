#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::video::frame::detail::capture {
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;

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

namespace sdl_rdp::video::frame {
using detail::capture::CaptureState;
using detail::capture::FrameCapture;
}
