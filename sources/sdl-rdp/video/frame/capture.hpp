#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::video::frame::detail::capture {
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

enum class CaptureState{ Failed, Idle, Captured };
class FrameCapture : private Pinned {
public:
       FrameCapture(PeerLink& link, FrameStore& store, PeerFrames& frames, DesktopLayout& desktop, FramePacing& pacing,
                    FrameStatistics& statistics, Encoder const& encoder) noexcept;
  auto Next() -> CaptureState;

private:
  auto Begin()              -> bool;
  auto Take()               -> Rect;
  auto Resize(Rect picture) -> bool;
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
