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
void State::EnsurePicture()
{
  std::scoped_lock lock(frame_guard);
  if (shadow) return;
  shadow = std::make_shared<std::vector<BYTE>>(std::size_t(width) * height * 4);
  frame_width = width; frame_height = height;
  if (current) current->Post({0, 0, int(width), int(height)});
}
void State::Resize(unsigned w, unsigned h)
{
  std::scoped_lock producer(producer_guard);
  std::scoped_lock session(session_guard);
  {
    std::scoped_lock lock(frame_guard);
    Picture(w, h);
    shadow.reset();
    width = w; height = h;
  }
  EnsurePicture();
  std::scoped_lock lock(peers_guard, frame_guard);
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
    return !current || !current->ack_enabled || current->acknowledged + 1 >= target;
  };
  if (timeout < 0) frame_changed.wait(lock, ready);
  else frame_changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return ready();
}
void Peer::FrameSent(std::size_t bytes)
{
  Expects(snapshot != nullptr, "sent frame has a snapshot");
  auto now = Clock::now();
  owner.trace.Line("frame", [&] { return std::format("id={} bytes={}", frame_id, bytes); });
  if (ack_enabled) pending.push_back({frame_id, sequence, bytes, now});
  if (first_sent == Clock::time_point{}) first_sent = now;
  auto elapsed = encoder.encode_time - encoded_at_start;
  encode_total += elapsed;
  encode_max = std::max(encode_max, elapsed);
  ++frames_sent;
}
void Peer::LogFrames()
{
  Expects(activated, "statistics belong to an activated connection");
  using Milliseconds = std::chrono::duration<double, std::milli>;
  auto phases = avc_frames ? std::format(" (convert {:.1f}, upload {:.1f}, nvenc {:.1f})",
    Milliseconds(avc_convert).count() / avc_frames, Milliseconds(avc_upload).count() / avc_frames,
    Milliseconds(avc_encode).count() / avc_frames) : std::string{};
  owner.Log(SDLRDP_LOG_INFO, std::format(
    "Frames: {} sent, {} coalesced; encode {:.1f} ms mean, {:.1f} ms max{}; acknowledgement {:.1f} ms mean, {:.1f} ms max, {} over 100 ms.",
    frames_sent, frames_coalesced, frames_sent ? Milliseconds(encode_total).count() / frames_sent : 0,
    Milliseconds(encode_max).count(), phases, ack_count ? Milliseconds(ack_total).count() / ack_count : 0,
    Milliseconds(ack_max).count(), ack_over_100ms));
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
  if (!Graphics() && ack_enabled && !ack_seen && first_sent != Clock::time_point{}
      && Clock::now() - first_sent >= std::chrono::milliseconds(250)) {
    ack_enabled = false;
    pending.clear();
    owner.frame_changed.notify_all();
  }
  return !ack_enabled || (pending.size() < 2 && (!Graphics() || gfx->Budget()));
}
void Peer::GraphicsDeadline()
{
  Expects(client != nullptr, "peer exists");
  if (!connection || Graphics() || Clock::now() < activated_at + GraphicsConnectionWait) return;
  gfx.reset();
  gfx_id = UINT32_MAX;
  gfx_attempted = true;
  owner.Log(SDLRDP_LOG_WARN, "GFX confirmation timed out; using legacy surface bits.");
  AnnounceConnection(encoder.codec);
}
DWORD Peer::Timeout()
{
  Expects(client != nullptr, "peer exists");
  if (connection) {
    auto remaining = activated_at + GraphicsConnectionWait - Clock::now();
    auto wait = DWORD(std::max<int64_t>(0, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
    return client->IsWriteBlocked(client.get()) ? std::min<DWORD>(wait, 5) : wait;
  }
  if (client->IsWriteBlocked(client.get())) return 5;
  std::scoped_lock lock(owner.frame_guard);
  if (Graphics() || !ack_enabled || ack_seen || first_sent == Clock::time_point{}) return INFINITE;
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - first_sent).count();
  return DWORD(std::max<int64_t>(1, 250 - elapsed));
}
BOOL Peer::Acknowledge(rdpContext* context, UINT32 id)
{
  Expects(context && context->peer, "acknowledgement has a peer");
  auto& self = Held(context->peer);
  if (!self.Graphics()) self.AcceptAcknowledgement(id);
  return TRUE;
}
void Peer::AcceptAcknowledgement(UINT32 id)
{
  auto& self = *this;
  std::scoped_lock lock(self.owner.frame_guard);
  auto found = std::ranges::find(self.pending, id, &Pending::id);
  if (found == self.pending.end()) return;
  self.acknowledged = found->sequence;
  auto now = Clock::now();
  owner.trace.Line("ack", [&] { return std::format("id={} age={:.1f}", id, std::chrono::duration<double, std::milli>(now - found->sent).count()); });
  for (auto frame = self.pending.begin(); frame != found + 1; ++frame) {
    auto elapsed = now - frame->sent;
    self.ack_total += elapsed;
    self.ack_max = std::max(self.ack_max, elapsed);
    ++self.ack_count;
    if (elapsed > std::chrono::milliseconds(100)) ++self.ack_over_100ms;
  }
  self.pending.erase(self.pending.begin(), found + 1);
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
