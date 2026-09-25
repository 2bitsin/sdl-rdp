#include <sdl-rdp/headless-client.test/frame/observer.hpp>

#include <freerdp/gdi/gdi.h>
#include <cstddef>
#include <cstdint>

namespace Headless {
FrameObserver::FrameObserver(Client& client)
    : update(client.Instance()->context->update), original(update->SurfaceFrameMarker) {
  Expects(!active, "one frame observer per thread");
  active = this;
  // abi: pSurfaceFrameMarker, BOOL is int
  update->SurfaceFrameMarker = [](rdpContext* context, SURFACE_FRAME_MARKER const* marker) -> int {
    active->Receive(*context, *marker);
    return true;
  };
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
auto FrameObserver::Frames() const -> std::vector<std::uint32_t> const& {
  return ids;
}
auto FrameObserver::ReceivedAt() const -> std::vector<Clock::time_point> const& {
  return received;
}
auto FrameObserver::AckFrame(std::uint32_t id) -> bool {
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
auto FrameObserver::Receive(rdpContext const& context, SURFACE_FRAME_MARKER const& marker) -> void {
  if (marker.frameAction != SURFACECMD_FRAMEACTION_END) return;
  ids.push_back(marker.frameId);
  received.push_back(Clock::now());
  auto*       gdi    = context.gdi;
  auto const* pixels = reinterpret_cast<std::uint32_t const*>(gdi->primary_buffer);
  coherent &= (pixels[0] & 0xffffff)
              == (pixels[(static_cast<std::ptrdiff_t>(gdi->height - 1)) * gdi->width] & 0xffffff);
}
}
