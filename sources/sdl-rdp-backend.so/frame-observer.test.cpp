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
auto FrameObserver::Ack() -> bool {
  if (ids.empty()) return false;
  auto sent = Clock::now();
  if (!update->SurfaceFrameAcknowledge(update->context, ids.back())) return false;
  ack_times.push_back(sent);
  return true;
}
auto FrameObserver::Frames() const -> std::vector<UINT32> const& {
  return ids;
}
auto FrameObserver::ReceivedAt() const -> std::vector<Clock::time_point> const& {
  return received;
}
auto FrameObserver::AckFrame(UINT32 id) -> bool {
  return update->SurfaceFrameAcknowledge(update->context, id);
}
auto FrameObserver::Acknowledgements() const -> std::vector<Clock::time_point> const& {
  return ack_times;
}
auto FrameObserver::Coherent() const -> bool {
  return coherent;
}
auto FrameObserver::Installed() const -> bool {
  return update != nullptr;
}
auto FrameObserver::Clear() -> void {
  ids.clear();
}
auto FrameObserver::Receive(rdpContext* context, SURFACE_FRAME_MARKER const* marker) -> BOOL {
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
