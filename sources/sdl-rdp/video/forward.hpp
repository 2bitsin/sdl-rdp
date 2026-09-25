#pragma once

namespace sdl_rdp::video::detail::display_control {
class DisplayControl;
}
namespace sdl_rdp::video::detail::encoder {
class Encoder;
}
namespace sdl_rdp::video::detail::graphics_link {
class GraphicsLink;
}
namespace sdl_rdp::video::detail::legacy_frame {
class LegacyFrame;
}
namespace sdl_rdp::video::detail::output_control {
class OutputControl;
}
namespace sdl_rdp::video::detail::peer_frames {
class PeerFrames;
}
namespace sdl_rdp::video::detail::scaler {
class Scaler;
}

namespace sdl_rdp::video {
using detail::display_control::DisplayControl;
using detail::encoder::Encoder;
using detail::graphics_link::GraphicsLink;
using detail::legacy_frame::LegacyFrame;
using detail::output_control::OutputControl;
using detail::peer_frames::PeerFrames;
using detail::scaler::Scaler;
}
