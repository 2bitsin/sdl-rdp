#pragma once
#include "client.hpp"

#include <freerdp/update.h>
#include <vector>

namespace Headless {
struct FrameObserver {
public:
                 FrameObserver(FrameObserver const&) = delete;
                 FrameObserver(FrameObserver&&)      = delete;
  explicit       FrameObserver(Client& client);
                 ~FrameObserver();
  FrameObserver& operator = (FrameObserver const&)   = delete;
  FrameObserver& operator = (FrameObserver&&)        = delete;

  bool                                  Ack();
  std::vector<UINT32> const&            Frames() const;
  std::vector<Clock::time_point> const& ReceivedAt() const;
  bool                                  AckFrame(UINT32 id);
  std::vector<Clock::time_point> const& Acknowledgements() const;
  bool                                  Coherent() const;
  bool                                  Installed() const;
  void                                  Clear();

private:
  static BOOL Receive(rdpContext* context, SURFACE_FRAME_MARKER const* marker);
  inline static thread_local FrameObserver* active    = nullptr;
  rdpUpdate*                                update;
  pSurfaceFrameMarker                       original;
  std::vector<UINT32>                       ids;
  std::vector<Clock::time_point>            received;
  bool                                      coherent  = true;
  std::vector<Clock::time_point>            ack_times;
};
}
