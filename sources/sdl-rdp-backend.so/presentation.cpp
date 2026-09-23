#include "_detail/state.hpp"

#include <cmath>
#include <freerdp/settings.h>
#include <numeric>
#include <stdexcept>

namespace Backend {
sdlrdp_rect State::Picture(unsigned w, unsigned h) const {
  w = w ? w : width;
  h = h ? h : height;
  Expects(w, "shadow width is positive");
  Expects(h, "shadow height is positive");
  if (!aspect.num || !aspect.den) return { 0, 0, int(w), int(h) };
  auto divisor     = std::gcd(aspect.num, aspect.den);
  uint64_t const n = aspect.num / divisor;
  uint64_t const d = aspect.den / divisor;
  auto units       = std::max((w + n - 1) / n, (h + d - 1) / d);
  if (units * n > 16383 || units * d > 65535)
    throw std::runtime_error("Aspect-corrected desktop exceeds RDP dimensions.");
  return { 0, 0, int(units * n), int(units * d) };
}
void State::EnsurePicture() {
  std::scoped_lock const lock(frame_guard);
  if (shadow) return;
  shadow       = std::make_shared<std::vector<BYTE>>(std::size_t(Avc::Aligned(width)) * Avc::Aligned(height) * 4);
  frame_width  = width;
  frame_height = height;
  if (current) current->Post({ 0, 0, int(width), int(height) });
}
void State::Resize(unsigned w, unsigned h) {
  Expects(w > 0, "picture width is positive");
  Expects(h > 0, "picture height is positive");
  std::scoped_lock const producer(producer_guard);
  std::scoped_lock const session(session_guard);
  if (!ChangePicture(w, h)) return;
  EnsurePicture();
  std::scoped_lock const lock(peers_guard, frame_guard);
  ++presented;
  for (auto const& peer : peers)
    if (peer->active) {
      peer->dirty.clear();
      peer->Post({ 0, 0, int(w), int(h) });
    }
}
bool State::ChangePicture(unsigned w, unsigned h) {
  Expects(w > 0, "picture width is positive");
  Expects(h > 0, "picture height is positive");
  std::scoped_lock const lock(frame_guard);
  Picture(w, h);
  if (width == w && height == h) return false;
  if (current) current->RestartRefresh();
  shadow.reset();
  width  = w;
  height = h;
  return true;
}
void State::SetAspect(sdlrdp_aspect value) {
  std::scoped_lock const lock(peers_guard, frame_guard);
  auto previous = aspect;
  aspect        = value;
  try {
    Picture();
  } catch (...) {
    aspect = previous;
    throw;
  }
  for (auto const& peer : peers)
    if (peer->active) peer->Post({ 0, 0, int(width), int(height) });
}
int State::WaitFrame(int timeout) {
  std::unique_lock lock(frame_guard);
  auto target = presented;
  auto ready  = [&] { return !current || !current->ack_enabled || current->acknowledged + 1 >= target; };
  if (timeout < 0)
    frame_changed.wait(lock, ready);
  else
    frame_changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return ready();
}
void Peer::FrameSent(std::size_t bytes) {
  Expects(snapshot != nullptr, "sent frame has a snapshot");
  auto now = Clock::now();
  MeasureWire(bytes);
  if (ack_enabled) pending.push_back({ frame_id, sequence, now });
  auto elapsed = encoder.encode_time - encoded_at_start;
  encode_total += elapsed;
  encode_max = std::max(encode_max, elapsed);
  ++frames_sent;
}
void Peer::LogFrames() {
  Expects(activated, "statistics belong to an activated connection");
  using Milliseconds = std::chrono::duration<double, std::milli>;
  auto phases        = avc_frames ? std::format(" (convert {:.1f}, upload {:.1f}, nvenc {:.1f})",
                                                Milliseconds(avc_convert).count() / double(avc_frames),
                                                Milliseconds(avc_upload).count() / double(avc_frames),
                                                Milliseconds(avc_encode).count() / double(avc_frames))
                                  : std::string{};
  owner.Log(SDLRDP_LOG_INFO,
            std::format(
                "Frames: {} sent, {} coalesced; encode {:.1f} ms mean, {:.1f} ms max{}; acknowledgement {:.1f} ms "
                "mean, {:.1f} ms max, {} over 100 ms, {} timed out. Send buffer: {:.1f} bytes mean, {} bytes max.",
                frames_sent, frames_coalesced,
                frames_sent ? Milliseconds(encode_total).count() / double(frames_sent) : 0,
                Milliseconds(encode_max).count(), phases,
                ack_count ? Milliseconds(ack_total).count() / double(ack_count) : 0, Milliseconds(ack_max).count(),
                ack_over_100ms, acks_timed_out, frames_sent ? double(outq_total) / double(frames_sent) : 0, outq_max));
}
bool Peer::Marker(UINT16 action) {
  Expects(client != nullptr, "client transport exists");
  Expects(client->context, "client context exists");
  if (!freerdp_settings_get_bool(client->context->settings, FreeRDP_FrameMarkerCommandEnabled)) return true;
  SURFACE_FRAME_MARKER const marker{ action, frame_id };
  return client->context->update->SurfaceFrameMarker(client->context, &marker);
}
bool Peer::Pacing() {
  Expects(client != nullptr, "peer exists");
  auto now = Clock::now();
  while (!pending.empty() && now - pending.front().sent >= AcknowledgementTimeout) {
    acknowledged = pending.front().sequence;
    pending.pop_front();
    ++acks_timed_out;
    owner.frame_changed.notify_all();
  }
  return !ack_enabled || pending.size() < (Graphics() ? gfx->FrameWindow() : FrameWindow);
}
void Peer::GraphicsDeadline() {
  Expects(client != nullptr, "peer exists");
  if (!connection || Graphics() || Clock::now() < activated_at + GraphicsConnectionWait) return;
  handle_count = 0;
  gfx.reset();
  gfx_id        = UINT32_MAX;
  gfx_attempted = true;
  owner.Log(SDLRDP_LOG_WARN, "GFX confirmation timed out; using legacy surface bits.");
  AnnounceConnection(encoder.codec);
}
DWORD Peer::Timeout() {
  Expects(client != nullptr, "peer exists");
  if (connection) {
    auto remaining = activated_at + GraphicsConnectionWait - Clock::now();
    auto wait      = DWORD(std::max<int64_t>(0, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
    return client->IsWriteBlocked(client.get()) ? std::min<DWORD>(wait, 5) : wait;
  }
  if (client->IsWriteBlocked(client.get())) return 5;
  std::scoped_lock const lock(owner.frame_guard);
  if (pending.empty()) return INFINITE;
  auto remaining = pending.front().sent + AcknowledgementTimeout - Clock::now();
  return DWORD(std::max<int64_t>(1, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
}
BOOL Peer::Acknowledge(rdpContext* context, UINT32 id) {
  Expects(context, "callback context exists");
  Expects(context->peer, "context belongs to a peer");
  auto& self = Held(context->peer);
  if (!self.Graphics()) self.AcceptAcknowledgement(id);
  return TRUE;
}
void Peer::RecordAcknowledgement(Clock::duration elapsed) {
  Expects(elapsed >= Clock::duration::zero(), "acknowledgement follows frame send");
  ack_total += elapsed;
  ack_max = std::max(ack_max, elapsed);
  ++ack_count;
  if (elapsed > std::chrono::milliseconds(100)) ++ack_over_100ms;
}
void Peer::AcceptAcknowledgement(UINT32 id) {
  Expects(client != nullptr, "acknowledgement belongs to a peer");
  auto& self = *this;
  std::scoped_lock const lock(self.owner.frame_guard);
  auto found = std::ranges::find(self.pending, id, &Pending::id);
  if (found == self.pending.end()) return;
  self.acknowledged = found->sequence;
  auto now = Clock::now();
  if (owner.trace.Enabled())
    trace_pending.push_back(owner.trace.Format("ack", [&] {
      return std::format("id={} age={:.1f}", id, std::chrono::duration<double, std::milli>(now - found->sent).count());
    }));
  RecordAcknowledgements(found, now);
  self.pending.erase(self.pending.begin(), found + 1);
  self.owner.frame_changed.notify_all();
  self.wake.Transition(WakeEvent::Phase::Pending);
}
BOOL Peer::Suppress(rdpContext* context, BYTE allow, RECTANGLE_16 const* /*unused*/) {
  Expects(context, "callback context exists");
  Expects(context->peer, "context belongs to a peer");
  auto& self = Held(context->peer);
  std::scoped_lock const lock(self.owner.frame_guard);
  self.suppressed = !allow;
  if (allow) self.Post({ 0, 0, int(self.owner.width), int(self.owner.height) });
  return TRUE;
}
} // namespace Backend
