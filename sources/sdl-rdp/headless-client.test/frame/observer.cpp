#include <sdl-rdp/headless-client.test/frame/observer.hpp>

#include <sdl-rdp/headless-client.test/client/framebuffer.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/gdi/gdi.h>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::headless_client_test::frame::detail::observer {
using sdl_rdp::headless_client_test::client::ClientUpdates;
using sdl_rdp::headless_client_test::client::Framebuffer;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

FrameObserver::FrameObserver(Client& client) : update(ClientUpdates(client)), original(update.SurfaceFrameMarker) {
  ObserverSet::Of(*update.context).Add(*this);
  // abi: pSurfaceFrameMarker, BOOL is int
  update.SurfaceFrameMarker = [](rdpContext* context, SURFACE_FRAME_MARKER const* marker) -> int {
    Expects(context != nullptr, "the frame marker names its client context");
    Expects(marker != nullptr, "frame marker is supplied");
    ObserverSet::Of(*context).Held<FrameObserver>()->Receive(*context, *marker);
    return true;
  };
}
FrameObserver::~FrameObserver() {
  update.SurfaceFrameMarker = original;
  ObserverSet::Of(*update.context).Remove<FrameObserver>();
}
auto FrameObserver::Ack() -> bool {
  if (ids.empty()) return false;
  auto sent = Clock::now();
  if (!update.SurfaceFrameAcknowledge(update.context, ids.back())) return false;
  ack_times.push_back(sent);
  return true;
}
auto FrameObserver::Frames() const -> std::vector<std::uint32_t> const& {
  return ids;
}
auto FrameObserver::ReceivedAt() const -> std::vector<Clock::time_point> const& {
  return received;
}
auto FrameObserver::AckFrame(std::uint32_t id) const -> bool {
  return update.SurfaceFrameAcknowledge(update.context, id);
}
auto FrameObserver::Acknowledgements() const -> std::vector<Clock::time_point> const& {
  return ack_times;
}
auto FrameObserver::Coherent() const -> bool {
  return coherent;
}
auto FrameObserver::Clear() -> void {
  ids.clear();
}
auto FrameObserver::Receive(rdpContext const& context, SURFACE_FRAME_MARKER const& marker) -> void {
  if (marker.frameAction != SURFACECMD_FRAMEACTION_END) return;
  ids.push_back(marker.frameId);
  received.push_back(Clock::now());
  Expects(context.gdi != nullptr, "decoded framebuffer exists");
  auto const pixels   = Framebuffer(*context.gdi);
  auto const last_row = Narrowed<std::size_t>(context.gdi->height - 1) * Narrowed<std::size_t>(context.gdi->width);
  Expects(last_row < pixels.size(), "the last row lies inside the framebuffer");
  coherent &= ((pixels.front() ^ pixels[last_row]) & 0xffffff) == 0;
}
}
