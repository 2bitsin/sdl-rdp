#pragma once
#include "contract.hpp"
#include "extent.hpp"
#include "frame-snapshot.hpp"
#include "picture-geometry.hpp"
#include "pinned.hpp"

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace Backend {
using FrameLock = std::unique_lock<std::mutex>;
class FrameStore : private Pinned {
public:
                          FrameStore(Extent size, sdlrdp_aspect aspect);
  [[nodiscard]] FrameLock Lock();
  bool                    Holds(FrameLock const& held) const noexcept;
  void                    Notify();
  bool                    WaitFor(FrameLock& held, int timeout, std::predicate auto ready) {
    Expects(Holds(held), "waiting holds the frame lock");
    if (timeout < 0)
      _changed.wait(held, ready);
    else
      _changed.wait_for(held, std::chrono::milliseconds(timeout), ready);
    return ready();
  }
  auto Read(std::invocable<FrameStore const&, FrameLock const&> auto query) {
    auto const held = Lock();
    return std::invoke(query, std::as_const(*this), held);
  }
  sdlrdp_rect          Picture(FrameLock const& held) const;
  sdlrdp_rect          Bounds(FrameLock const& held) const;
  FrameSnapshot const& Snapshot(FrameLock const& held) const;
  FrameSnapshot        Previous(FrameLock const& held, Extent size) const;
  uint64_t             Presented(FrameLock const& held) const;
  bool                 Publish(FrameLock const& held, std::shared_ptr<std::vector<BYTE> const> next, Extent size);
  bool                 Ensure(FrameLock const& held);
  bool                 Resize(FrameLock const& held, Extent size);
  void                 SetAspect(FrameLock const& held, sdlrdp_aspect value);

private:
  std::mutex              _guard;
  std::condition_variable _changed;
  PictureGeometry         _geometry;
  FrameSnapshot           _shadow;
  uint64_t                _presented{ };
};
}
