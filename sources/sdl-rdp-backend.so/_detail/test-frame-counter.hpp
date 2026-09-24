#pragma once
#include "client.hpp"

#include <freerdp/freerdp.h>

namespace BackendGate {
class FrameCounter {
public:
                FrameCounter(FrameCounter const&) = delete;
                FrameCounter(FrameCounter&&)      = delete;
  explicit      FrameCounter(Headless::Client& client);
                ~FrameCounter();
  FrameCounter& operator = (FrameCounter const&)  = delete;
  FrameCounter& operator = (FrameCounter&&)       = delete;
  static BOOL   ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command);
  static BOOL   ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command);
  unsigned      Frames() const;
  unsigned      BitmapPdus() const;

private:
  unsigned                                 frames      = 0;
  unsigned                                 bitmap_pdus = 0;
  inline static thread_local FrameCounter* active      = nullptr;
  rdpUpdate*                               update;
  pSurfaceBits                             surface;
  pBitmapUpdate                            bitmap;
};
}
