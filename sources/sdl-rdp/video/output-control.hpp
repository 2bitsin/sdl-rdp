#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

#include <cstdint>

namespace sdl_rdp::video::detail::output_control {
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::frame::FramePacing;

class OutputControl : private Pinned {
public:
       OutputControl(PeerLink& link, GraphicsLink const& graphics, FramePacing& pacing, Activation& activation,
                     PeerFrames& frames) noexcept;
  auto Acknowledge(std::uint32_t id) -> void;
  auto Suppress(bool allow)          -> void;

private:
  PeerLink&           _link;
  GraphicsLink const& _graphics;
  FramePacing&        _pacing;
  Activation&         _activation;
  PeerFrames&         _frames;
};
}

namespace sdl_rdp::video {
using detail::output_control::OutputControl;
}
