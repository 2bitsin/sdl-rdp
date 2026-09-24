#include "_detail/frame-observer.hpp"

#include <cstddef>
#include <freerdp/gdi/gdi.h>

namespace Headless {
FrameObserver::FrameObserver(Client& client)
    : update(client.Instance()->context->update), original(update->SurfaceFrameMarker) {
  Expects(!active, "one frame observer per thread");
  active                     = this;
  update->SurfaceFrameMarker = Receive;
}
FrameObserver::~FrameObserver() {
  update->SurfaceFrameMarker = original;
  active                     = nullptr;
}
bool FrameObserver::Ack() {
  if (ids.empty()) return false;
  auto sent = Clock::now();
  if (!update->SurfaceFrameAcknowledge(update->context, ids.back())) return false;
  ack_times.push_back(sent);
  return true;
}
std::vector<UINT32> const& FrameObserver::Frames() const {
  return ids;
}
std::vector<Clock::time_point> const& FrameObserver::ReceivedAt() const {
  return received;
}
bool FrameObserver::AckFrame(UINT32 id) {
  return update->SurfaceFrameAcknowledge(update->context, id);
}
std::vector<Clock::time_point> const& FrameObserver::Acknowledgements() const {
  return ack_times;
}
bool FrameObserver::Coherent() const {
  return coherent;
}
bool FrameObserver::Installed() const {
  return update != nullptr;
}
void FrameObserver::Clear() {
  ids.clear();
}
BOOL FrameObserver::Receive(rdpContext* context, SURFACE_FRAME_MARKER const* marker) {
  if (marker->frameAction != SURFACECMD_FRAMEACTION_END) return TRUE;
  active->ids.push_back(marker->frameId);
  active->received.push_back(Clock::now());
  auto*       gdi    = context->gdi;
  auto const* pixels = reinterpret_cast<UINT32 const*>(gdi->primary_buffer);
  active->coherent &=
      (pixels[0] & 0xffffff) == (pixels[(static_cast<std::ptrdiff_t>(gdi->height - 1)) * gdi->width] & 0xffffff);
  return TRUE;
}
}
