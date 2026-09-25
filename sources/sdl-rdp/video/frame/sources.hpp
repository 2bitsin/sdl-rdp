#pragma once
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <functional>

namespace sdl_rdp::video::frame::detail::sources {

struct FrameSources {
  std::reference_wrapper<PeerFrames>  frames;
  std::reference_wrapper<FramePacing> pacing;
  std::reference_wrapper<Encoder>     encoder;
  std::reference_wrapper<Scaler>      scaler;
};
}

namespace sdl_rdp::video::frame {
using detail::sources::FrameSources;
}
