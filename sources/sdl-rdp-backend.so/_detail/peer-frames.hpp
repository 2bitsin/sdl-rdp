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
  explicit                        PeerFrames(FrameStore& store) noexcept;
  void                            Post(FrameLock const& held, sdlrdp_rect area);
  void                            Repaint(FrameLock const& held, sdlrdp_rect area);
  void                            Refresh();
  void                            CountPresent(FrameLock const& held);
  bool                            Dirty(FrameLock const& held) const;
  bool                            Pending(FrameLock const& held) const;
  uint64_t                        Capture(FrameLock const& held);
  void                            Invalidate(FrameLock const& held);
  void                            Include();
  void                            Resend();
  void                            Complete(FrameLock const& held);
  FrameSnapshot const&            Snapshot() const              noexcept;
  std::vector<sdlrdp_rect> const& Sending() const               noexcept;
  uint64_t                        Sequence() const              noexcept;

private:
  FrameStore&   _store;
  Region        _dirty;
  Region        _sending;
  FrameSnapshot _snapshot;
  uint64_t      _sequence{ };
  uint64_t      _presents{ };
};
void ExpectCaptured(PeerFrames const& frames);
}
