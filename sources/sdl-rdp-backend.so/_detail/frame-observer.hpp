#pragma once
#include "client.hpp"

#include <freerdp/update.h>
#include <vector>

namespace Headless {
struct FrameObserver {
public:
           FrameObserver(FrameObserver const&)                 = delete;
           FrameObserver(FrameObserver&&)                      = delete;
  explicit FrameObserver(Client& client);
           ~FrameObserver();
  auto     operator = (FrameObserver const&) -> FrameObserver& = delete;
  auto     operator = (FrameObserver&&)      -> FrameObserver& = delete;

  auto Ack()                    -> bool;
  auto Frames() const           -> std::vector<UINT32> const&;
  auto ReceivedAt() const       -> std::vector<Clock::time_point> const&;
  auto AckFrame(UINT32 id)      -> bool;
  auto Acknowledgements() const -> std::vector<Clock::time_point> const&;
  auto Coherent() const         -> bool;
  auto Installed() const        -> bool;
  auto Clear()                  -> void;

private:
  static auto Receive(rdpContext* context, SURFACE_FRAME_MARKER const* marker) -> BOOL;
  inline static thread_local FrameObserver* active    = nullptr;
  rdpUpdate*                                update;
  pSurfaceFrameMarker                       original;
  std::vector<UINT32>                       ids;
  std::vector<Clock::time_point>            received;
  bool                                      coherent  = true;
  std::vector<Clock::time_point>            ack_times;
};
}
