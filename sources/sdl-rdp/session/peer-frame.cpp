#include <sdl-rdp/session/peer-frame.hpp>

#include <utility>

namespace sdl_rdp::session::detail::peer_frame {
PeerFrame::PeerFrame(PeerSet& peers, FrameStore& frames)
    : _peers{ peers }, _held{ peers.Lock() }, _frame{ frames.Lock() } { }
auto PeerFrame::Frame() const noexcept -> FrameLock const& {
  return _frame;
}
auto PeerFrame::ReleaseFrame() && noexcept -> FrameLock {
  return std::move(_frame);
}
}
