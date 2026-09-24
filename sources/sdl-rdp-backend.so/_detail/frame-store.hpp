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
  [[nodiscard]] auto Lock()                                                           -> FrameLock;
  auto               Holds(FrameLock const& held) const noexcept                      -> bool;
  auto               Notify()                                                         -> void;
  auto               WaitFor(FrameLock& held, int timeout, std::predicate auto ready) -> bool {
    Expects(Holds(held), "waiting holds the frame lock");
    if (timeout < 0)
      _changed.wait(held, ready);
    else
      _changed.wait_for(held, std::chrono::milliseconds(timeout), ready);
    return ready();
  }
  auto Read(std::invocable<FrameStore const&, FrameLock const&> auto query) -> decltype(auto) {
    auto const held = Lock();
    return std::invoke(query, std::as_const(*this), held);
  }
  auto Picture(FrameLock const& held) const                  -> sdlrdp_rect;
  auto Bounds(FrameLock const& held) const                   -> sdlrdp_rect;
  auto Snapshot(FrameLock const& held) const                 -> FrameSnapshot const&;
  auto Previous(FrameLock const& held, Extent size) const    -> FrameSnapshot;
  auto Presented(FrameLock const& held) const                -> uint64_t;
  auto Publish(FrameLock const& held, std::shared_ptr<std::vector<BYTE> const> next, Extent size) -> bool;
  auto Ensure(FrameLock const& held)                         -> bool;
  auto Resize(FrameLock const& held, Extent size)            -> bool;
  auto SetAspect(FrameLock const& held, sdlrdp_aspect value) -> void;

private:
  std::mutex              _guard;
  std::condition_variable _changed;
  PictureGeometry         _geometry;
  FrameSnapshot           _shadow;
  uint64_t                _presented{ };
};
}
