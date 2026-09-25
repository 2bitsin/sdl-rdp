#pragma once
#include <sdl-rdp/picture/frame-snapshot.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/region.hpp>

#include <cstdint>
#include <vector>

namespace sdl_rdp::video::detail::peer_frames {
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameSnapshot;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Region;

class PeerFrames : private Pinned {
public:
  explicit PeerFrames(FrameStore& store) noexcept;
  auto     Post(FrameLock const& held, sdlrdp_rect area)    -> void;
  auto     Repaint(FrameLock const& held, sdlrdp_rect area) -> void;
  auto     Refresh()                                        -> void;
  auto     CountPresent(FrameLock const& held)              -> void;
  auto     Dirty(FrameLock const& held) const               -> bool;
  auto     Pending(FrameLock const& held) const             -> bool;
  auto     Capture(FrameLock const& held)                   -> std::uint64_t;
  auto     Invalidate(FrameLock const& held)                -> void;
  auto     Include()                                        -> void;
  auto     Resend()                                         -> void;
  auto     Complete(FrameLock const& held)                  -> void;
  auto     Snapshot() const noexcept                        -> FrameSnapshot const&;
  auto     Sending() const noexcept                         -> std::vector<sdlrdp_rect> const&;
  auto     Sequence() const noexcept                        -> std::uint64_t;

private:
  FrameStore&   _store;
  Region        _dirty;
  Region        _sending;
  FrameSnapshot _snapshot;
  std::uint64_t _sequence{ };
  std::uint64_t _presents{ };
};
auto ExpectCaptured(PeerFrames const& frames) -> void;
}

namespace sdl_rdp::video {
using detail::peer_frames::ExpectCaptured;
using detail::peer_frames::PeerFrames;
}
