#include "_detail/state.hpp"

#include <algorithm>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/session.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>

namespace Backend {
bool Peer::TransportStep(std::stop_token const& quit, std::span<HANDLE const> ready) {
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  {
    std::scoped_lock const lock(owner.session_guard);
    if (quit.stop_requested()) return false;
    if (!client->CheckFileDescriptor(client.get()) || !Channels(ready) || !SoundChannel(ready) || !Drain()) {
      TransportEnded();
      return false;
    }
  }
  return EncodeAndSend(quit);
}
void Peer::TransitionEncode(EncodeState next) {
  switch (encode_state) {
  case EncodeState::Idle:
    Expects(next != EncodeState::LegacyReady, "encoding precedes legacy writes");
    break;
  case EncodeState::Legacy:
    Expects(next == EncodeState::LegacyReady, "legacy encoding produces packets");
    break;
  case EncodeState::Graphics:
  case EncodeState::LegacyReady:
    Expects(next == EncodeState::Idle, "completed encoding returns to idle");
    break;
  default:
    utilities::Unreachable(encode_state);
  }
  encode_state = next;
}
bool Peer::SendEncoded(bool encoded, std::stop_token const& quit) {
  std::scoped_lock const lock(owner.session_guard);
  auto kind = encode_state;
  TransitionEncode(kind == EncodeState::Legacy ? EncodeState::LegacyReady : EncodeState::Idle);
  if (quit.stop_requested()) return false;
  if (!active) return true;
  if (encoded && (kind == EncodeState::Legacy ? legacy.Send(*this) : gfx->Send())) {
    if (kind == EncodeState::Legacy && !snapshot) TransitionEncode(EncodeState::Idle);
    return true;
  }
  TransportEnded();
  return false;
}
bool Peer::ReadyFrame() {
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  std::scoped_lock const lock(owner.frame_guard);
  wake.Transition(WakeEvent::Phase::Idle);
  if (client->IsWriteBlocked(client.get())) {
    auto previous = refresh.Rate();
    refresh.Blocked(Clock::now());
    PublishRefresh(previous);
    return false;
  }
  if (!freerdp_is_active_state(client->context)) return false;
  if (resizing) {
    resizing = false;
    RestartRefresh();
    if (!dirty.empty()) {
      snapshot.reset();
      dirty.Add({ 0, 0, int(owner.frame_width), int(owner.frame_height) });
    }
  }
  if (refresh.AwaitingEmpty()) {
    auto previous = refresh.Rate();
    refresh.Drained(SampleWire(socket_descriptor));
    PublishRefresh(previous);
  }
  return Pacing() && !suppressed;
}
bool Peer::EncodeAndSend(std::stop_token const& quit) {
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  switch (encode_state) {
  case EncodeState::Idle:
  case EncodeState::LegacyReady:
    return true;
  case EncodeState::Legacy:
    return SendEncoded(legacy.Encode(*this), quit);
  case EncodeState::Graphics:
    Expects(gfx != nullptr, "graphics channel exists");
    Expects(snapshot != nullptr, "immutable frame exists");
    return SendEncoded(gfx->Encode(), quit);
  default:
    utilities::Unreachable(encode_state);
  }
}
bool Peer::Drain() {
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  if (!active) return true;
  if (client->DrainOutputBuffer(client.get()) < 0) return false;
  if (!ReadyFrame()) return true;
  if (connection) return true;
  if (!SendPointer()) return false;
  if (!snapshot && !BeginFrame()) return false;
  if (!snapshot || resizing) return true;
  return PrepareFrame();
}
bool Peer::PrepareFrame() {
  if (encode_state == EncodeState::LegacyReady) {
    if (!legacy.Send(*this)) return false;
    if (!snapshot) TransitionEncode(EncodeState::Idle);
    return true;
  }
  if (!(Graphics() ? gfx->Prepare() : legacy.Prepare(*this))) return false;
  TransitionEncode(Graphics() ? EncodeState::Graphics : EncodeState::Legacy);
  return true;
}
BOOL Peer::Capabilities(freerdp_peer* client) {
  auto& self = Held(client);
  if (!AuthenticateSettings(client)) return FALSE;
  auto* settings = client->context->settings;
  std::scoped_lock const lock(self.owner.frame_guard);
  if (!self.activated) {
    self.RestartRefresh();
    self.screen_width  = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
    self.screen_height = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
  }
  auto depth = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
  if (depth != 16 && depth != 24 && depth != 32) {
    self.owner.Log(SDLRDP_LOG_WARN, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return FALSE;
  }
  auto picture = self.resizing ? self.desktop : self.owner.Picture();
  return freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w) &&
         freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h);
}
sdlrdp_rect Peer::CaptureFrame() {
  Expects(!resizing, "no resize in flight");
  std::scoped_lock const lock(owner.frame_guard);
  if (dirty.empty() || !owner.shadow) return desktop;
  auto picture = owner.Picture();
  if (picture.w != desktop.w || picture.h != desktop.h || snapshot_width != owner.frame_width ||
      snapshot_height != owner.frame_height)
    RestartRefresh();
  snapshot         =  owner.shadow;
  snapshot_width   =  owner.frame_width;
  snapshot_height  =  owner.frame_height;
  sequence         =  owner.presented;
  frames_coalesced += dirty_presents ? dirty_presents - 1 : 0;
  dirty_presents   =  0;
  encoded_at_start =  encoder.encode_time;
  sending.Swap(dirty);
  dirty.clear();
  return picture;
}
bool Peer::ResizeDesktop(sdlrdp_rect picture) {
  Expects(client != nullptr, "peer exists");
  Expects(snapshot != nullptr, "resize has a frame");
  desktop  = picture;
  resizing = true;
  auto* settings = client->context->settings;
  if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w) ||
      !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h) ||
      !client->context->update->DesktopResize(client->context))
    return false;
  Ensures(resizing, "desktop resize remains in flight until the client is active");
  sending.clear();
  sending.Add({ 0, 0, int(snapshot_width), int(snapshot_height) });
  return true;
}
bool Peer::BeginFrame() {
  Expects(!resizing, "no resize in flight");
  Expects(freerdp_is_active_state(client->context), "resize requires an active client");
  auto picture = CaptureFrame();
  if (!snapshot) return true;
  if (picture.w != desktop.w || picture.h != desktop.h)
    if (!ResizeDesktop(picture)) return false;
  ++frame_id;
  return true;
}
} // namespace Backend
