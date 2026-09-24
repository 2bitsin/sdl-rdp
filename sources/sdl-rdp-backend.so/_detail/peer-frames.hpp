#pragma once
#include "frame-snapshot.hpp"
#include "frame-store.hpp"
#include "pinned.hpp"
#include "region.hpp"

#include <cstdint>
#include <vector>

namespace Backend {
class PeerFrames : private Pinned {
public:
  explicit PeerFrames(FrameStore& store) noexcept;
  auto     Post(FrameLock const& held, sdlrdp_rect area)    -> void;
  auto     Repaint(FrameLock const& held, sdlrdp_rect area) -> void;
  auto     Refresh()                                        -> void;
  auto     CountPresent(FrameLock const& held)              -> void;
  auto     Dirty(FrameLock const& held) const               -> bool;
  auto     Pending(FrameLock const& held) const             -> bool;
  auto     Capture(FrameLock const& held)                   -> uint64_t;
  auto     Invalidate(FrameLock const& held)                -> void;
  auto     Include()                                        -> void;
  auto     Resend()                                         -> void;
  auto     Complete(FrameLock const& held)                  -> void;
  auto     Snapshot() const noexcept                        -> FrameSnapshot const&;
  auto     Sending() const noexcept                         -> std::vector<sdlrdp_rect> const&;
  auto     Sequence() const noexcept                        -> uint64_t;

private:
  FrameStore&   _store;
  Region        _dirty;
  Region        _sending;
  FrameSnapshot _snapshot;
  uint64_t      _sequence{ };
  uint64_t      _presents{ };
};
auto ExpectCaptured(PeerFrames const& frames) -> void;
}
