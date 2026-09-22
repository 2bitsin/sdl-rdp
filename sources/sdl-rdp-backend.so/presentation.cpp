#include "_detail/state.hpp"
#include <freerdp/settings.h>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace Backend {
sdlrdp_rect State::Picture(unsigned w, unsigned h) const
{
  w = w ? w : width; h = h ? h : height;
  Expects(w && h, "shadow dimensions are positive");
  if (!aspect.num || !aspect.den) return {0, 0, int(w), int(h)};
  auto divisor = std::gcd(aspect.num, aspect.den);
  uint64_t n = aspect.num / divisor, d = aspect.den / divisor;
  auto units = std::max((w + n - 1) / n, (h + d - 1) / d);
  if (units * n > 16383 || units * d > 65535)
    throw std::runtime_error("Aspect-corrected desktop exceeds RDP dimensions.");
  return {0, 0, int(units * n), int(units * d)};
}
void State::Resize(unsigned w, unsigned h)
{
  std::scoped_lock producer(producer_guard);
  auto next = std::make_shared<std::vector<BYTE>>(std::size_t(w) * h * 4);
  std::scoped_lock lock(peers_guard, frame_guard);
  Picture(w, h);
  shadow = std::move(next);
  frame_width = width = w; frame_height = height = h;
  ++presented;
  for (auto const& peer : peers) if (peer->active) {
    peer->dirty.clear();
    peer->Post({0, 0, int(w), int(h)});
  }
}
void State::SetAspect(sdlrdp_aspect value)
{
  std::scoped_lock lock(peers_guard, frame_guard);
  auto previous = aspect;
  aspect = value;
  try { Picture(); } catch (...) { aspect = previous; throw; }
  for (auto const& peer : peers) if (peer->active)
    peer->Post({0, 0, int(width), int(height)});
}
int State::WaitFrame(int timeout)
{
  std::unique_lock lock(frame_guard);
  auto target = presented;
  auto ready = [&] {
    return !current || !current->ack_enabled || current->acknowledged >= target;
  };
  if (timeout < 0) frame_changed.wait(lock, ready);
  else frame_changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return ready();
}
bool Peer::Marker(UINT16 action)
{
  Expects(client && client->context, "frame marker destination exists");
  if (!freerdp_settings_get_bool(client->context->settings, FreeRDP_FrameMarkerCommandEnabled))
    return true;
  SURFACE_FRAME_MARKER marker{action, frame_id};
  return client->context->update->SurfaceFrameMarker(client->context, &marker);
}
bool Peer::Pacing()
{
  Expects(client != nullptr, "peer exists");
  // 250 ms allows fifteen 60 Hz refresh periods for a first acknowledgement.
  if (ack_enabled && !ack_seen && first_sent != Clock::time_point{}
      && Clock::now() - first_sent >= std::chrono::milliseconds(250)) {
    ack_enabled = false;
    pending.clear();
    owner.frame_changed.notify_all();
  }
  return !ack_enabled || pending.size() < 2;
}
DWORD Peer::Timeout()
{
  Expects(client != nullptr, "peer exists");
  if (client->IsWriteBlocked(client.get())) return 5;
  std::scoped_lock lock(owner.frame_guard);
  if (!ack_enabled || ack_seen || first_sent == Clock::time_point{}) return INFINITE;
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - first_sent).count();
  return DWORD(std::max<int64_t>(1, 250 - elapsed));
}
BOOL Peer::Acknowledge(rdpContext* context, UINT32 id)
{
  Expects(context && context->peer, "acknowledgement has a peer");
  auto& self = Held(context->peer);
  std::scoped_lock lock(self.owner.frame_guard);
  auto found = std::ranges::find(self.pending, id, &Pending::id);
  if (found == self.pending.end()) return TRUE;
  self.acknowledged = found->sequence;
  self.pending.erase(self.pending.begin(), found + 1);
  auto now = Clock::now();
  if (self.ack_seen) {
    auto ms = std::chrono::duration<double, std::milli>(now - self.last_ack).count();
    self.ack_interval = self.ack_interval ? self.ack_interval * 0.8 + ms * 0.2 : ms;
    auto rate = unsigned(std::clamp(1000000.0 / self.ack_interval, 1.0, 1000000.0));
    if (!self.refresh || std::abs(double(rate) - self.refresh) > self.refresh * 0.05) {
      self.refresh = rate;
      self.owner.Push({.type = SDLRDP_REFRESH, .refresh = {rate}});
    }
  }
  self.ack_seen = true;
  self.last_ack = now;
  self.owner.frame_changed.notify_all();
  SetEvent(self.wake.get());
  return TRUE;
}
BOOL Peer::Suppress(rdpContext* context, BYTE allow, RECTANGLE_16 const*)
{
  Expects(context && context->peer, "suppression has a peer");
  auto& self = Held(context->peer);
  std::scoped_lock lock(self.owner.frame_guard);
  self.suppressed = !allow;
  if (allow) self.Post({0, 0, int(self.owner.width), int(self.owner.height)});
  return TRUE;
}
}
