#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

namespace sdl_rdp::video::frame::detail::gate {
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;

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

namespace sdl_rdp::video::frame {
using detail::gate::FrameGate;
}
