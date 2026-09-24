#include "_detail/test-frame-counter.hpp"

#include "_detail/contract.hpp"

#include <algorithm>
#include <freerdp/gdi/gdi.h>
#include <span>
#include <utility>

namespace BackendGate {
using utilities::Expects;
FrameCounter::FrameCounter(Headless::Client& client)
    : update(client.Instance()->context->update), surface(update->SurfaceBits), bitmap(update->BitmapUpdate) {
  Expects(!active, "no observer is already installed");
  Expects(surface, "surface callback is installed");
  Expects(bitmap, "bitmap callback is installed");
  active               = this;
  update->SurfaceBits  = ReceiveSurface;
  update->BitmapUpdate = ReceiveBitmap;
}
FrameCounter::~FrameCounter() {
  update->SurfaceBits  = surface;
  update->BitmapUpdate = bitmap;
  active               = nullptr;
}
BOOL FrameCounter::ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
  Expects(active, "observer is installed");
  Expects(command, "wire command is supplied");
  auto result = active->surface(context, command);
  if (result && std::cmp_equal(command->destBottom, context->gdi->height)) ++active->frames;
  return result;
}
BOOL FrameCounter::ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
  Expects(active, "observer is installed");
  Expects(command, "wire command is supplied");
  auto result = active->bitmap(context, command);
  ++active->bitmap_pdus;
  if (result && std::ranges::any_of(std::span(command->rectangles, command->number), [=](auto const& rectangle) {
        return rectangle.destBottom + 1 == context->gdi->height;
      }))
    ++active->frames;
  return result;
}
unsigned FrameCounter::Frames() const {
  return frames;
}
unsigned FrameCounter::BitmapPdus() const {
  return bitmap_pdus;
}
}
