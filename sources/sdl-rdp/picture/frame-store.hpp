#pragma once
#include <sdl-rdp/picture/frame-snapshot.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace sdl_rdp::picture::detail::frame_store {
using sdl_rdp::utilities::Deadline;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;

using FrameLock = std::unique_lock<std::mutex>;
class FrameStore : private Pinned {
public:
                     FrameStore(Extent size, sdlrdp_aspect aspect);
  [[nodiscard]] auto Lock()                                                                 -> FrameLock;
  auto               Holds(FrameLock const& held) const noexcept                            -> bool;
  auto               Notify()                                                               -> void;
  auto               WaitFor(FrameLock& held, Deadline deadline, std::predicate auto ready) -> bool {
    Expects(Holds(held), "waiting holds the frame lock");
    return _changed.wait_until(held, deadline, ready);
  }
  auto Read(std::invocable<FrameStore const&, FrameLock const&> auto query) -> decltype(auto) {
    auto const held = Lock();
    return std::invoke(query, std::as_const(*this), held);
  }
  auto Picture(FrameLock const& held) const                  -> sdlrdp_rect;
  auto Bounds(FrameLock const& held) const                   -> sdlrdp_rect;
  auto Snapshot(FrameLock const& held) const                 -> FrameSnapshot const&;
  auto Previous(FrameLock const& held, Extent size) const    -> FrameSnapshot;
  auto Presented(FrameLock const& held) const                -> std::uint64_t;
  auto Publish(FrameLock const& held, std::shared_ptr<std::vector<std::uint8_t> const> next, Extent size) -> bool;
  auto Ensure(FrameLock const& held)                         -> bool;
  auto Resize(FrameLock const& held, Extent size)            -> bool;
  auto SetAspect(FrameLock const& held, sdlrdp_aspect value) -> void;

private:
  std::mutex              _guard;
  std::condition_variable _changed;
  PictureGeometry         _geometry;
  FrameSnapshot           _shadow;
  std::uint64_t           _presented{ };
};
}

namespace sdl_rdp::picture {
using detail::frame_store::FrameLock;
using detail::frame_store::FrameStore;
}
