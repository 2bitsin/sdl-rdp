#pragma once

namespace sdl_rdp::video::frame::detail::capture {
class FrameCapture;
}
namespace sdl_rdp::video::frame::detail::gate {
class FrameGate;
}
namespace sdl_rdp::video::frame::detail::pacing {
class FramePacing;
}
namespace sdl_rdp::video::frame::detail::sender {
class FrameSender;
}
namespace sdl_rdp::video::frame::detail::statistics {
class FrameStatistics;
}

namespace sdl_rdp::video::frame {
using detail::capture::FrameCapture;
using detail::gate::FrameGate;
using detail::pacing::FramePacing;
using detail::sender::FrameSender;
using detail::statistics::FrameStatistics;
}
