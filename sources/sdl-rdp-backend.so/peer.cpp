#include "_detail/state.hpp"
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <freerdp/settings.h>
#include <freerdp/session.h>
#include <winpr/crypto.h>
#include <freerdp/input.h>
#include <freerdp/update.h>
#include <winpr/synch.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <stdexcept>
#include <freerdp/channels/wtsvc.h>

namespace Backend {
namespace {
sdlrdp_event Connected(rdpSettings const* settings)
{
  Expects(settings != nullptr, "settings exist");
  sdlrdp_event event{.type = SDLRDP_CONNECTED};
  event.connected.width = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
  event.connected.height = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
  event.connected.bpp = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
  auto name = freerdp_settings_get_string(settings, FreeRDP_ClientHostname);
  if (name) std::strncpy(event.connected.client_name, name, sizeof(event.connected.client_name) - 1);
  return event;
}
bool SendCookie(rdpContext* context)
{
  Expects(context != nullptr, "session context exists");
  ARC_SC_PRIVATE_PACKET cookie{};
  cookie.cbLen = 28;
  cookie.version = AUTO_RECONNECT_VERSION_1;
  cookie.logonId = 1;
  if (winpr_RAND(cookie.arcRandomBits, sizeof(cookie.arcRandomBits)) != 0) return false;
  if (!freerdp_settings_set_pointer_len(context->settings, FreeRDP_ServerAutoReconnectCookie,
                                        &cookie, 1)) return false;
  logon_info_ex info{};
  info.haveCookie = TRUE;
  info.LogonId = cookie.logonId;
  std::ranges::copy(cookie.arcRandomBits, info.ArcRandomBits);
  return context->update->SaveSessionInfo(context, INFO_TYPE_LOGON_EXTENDED_INF, &info);
}
}
Peer& Peer::Held(freerdp_peer* client)
{
  Expects(client && client->ContextExtra, "peer has an owner");
  return *static_cast<Peer*>(client->ContextExtra);
}
Peer::Peer(PeerHandle accepted, State& state)
 : client(std::move(accepted)), owner(state), wake(CreateEvent(nullptr, TRUE, FALSE, nullptr))
{
  auto raw = client.get();
  Expects(raw != nullptr, "accepted peer exists");
  if (!wake) throw std::runtime_error("peer event allocation failed");
  raw->ContextSize = sizeof(rdpContext);
  raw->ContextExtra = this;
  raw->Activate = Activate;
  raw->Capabilities = Capabilities;
  raw->PostConnect = [](freerdp_peer*) -> BOOL { return TRUE; };
  if (!freerdp_peer_context_new(raw)) throw std::runtime_error("peer context failed");
  channels = WTSOpenServerA(reinterpret_cast<char*>(raw->context));
  if (!channels || channels == INVALID_HANDLE_VALUE) throw std::runtime_error("Channel manager allocation failed.");
  raw->context->update->SurfaceFrameAcknowledge = Acknowledge;
  raw->context->update->SuppressOutput = Suppress;
  auto input = raw->context->input;
  input->KeyboardEvent = Keyboard;
  input->MouseEvent = Mouse;
  input->ExtendedMouseEvent = ExtendedMouse;
}
Peer::~Peer()
{
  thread.request_stop();
  SetEvent(wake.get());
  if (thread.joinable()) thread.join();
  sound.reset();
  disp.reset();
  if (channels) WTSCloseServer(channels);
}
void Peer::Start()
{
  Expects(!thread.joinable(), "peer starts once");
  thread = std::jthread([this](std::stop_token quit) { Serve(quit); });
}
bool Peer::Configure()
{
  Expects(client && client->context, "peer context exists");
  sdlrdp_rect picture;
  { std::scoped_lock lock(owner.frame_guard); picture = owner.Picture(); }
  auto settings = client->context->settings;
  std::unique_ptr<rdpPrivateKey, Releases<freerdp_key_free>> key(
    freerdp_key_new_from_file(owner.credentials.key.c_str()));
  std::unique_ptr<rdpCertificate, Releases<freerdp_certificate_free>> cert(
    freerdp_certificate_new_from_file(owner.credentials.certificate.c_str()));
  if (!key || !cert) return false;
  // These two pointer setters transfer ownership despite the generic API's copy documentation.
  if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key.get(), 1)) return false;
  key.release();
  if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert.get(), 1)) return false;
  cert.release();
  return freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE)
    && freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_WaitForOutputBufferFlush, FALSE)
    && freerdp_settings_set_uint32(settings, FreeRDP_EncryptionLevel, ENCRYPTION_LEVEL_CLIENT_COMPATIBLE)
    && freerdp_settings_set_bool(settings, FreeRDP_FrameMarkerCommandEnabled, TRUE)
    && freerdp_settings_set_uint32(settings, FreeRDP_FrameAcknowledge, 2)
    && freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_SuppressOutput, TRUE)
    && freerdp_settings_set_uint32(settings, FreeRDP_LargePointerFlag, LARGE_POINTER_FLAG_96x96 | LARGE_POINTER_FLAG_384x384)
    && freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w)
    && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h);
}
DWORD Peer::EventHandles(std::span<HANDLE> handles)
{
  static constexpr DWORD appended_handles = 3;
  Expects(handles.size() > appended_handles, "event array has room for transport and peer handles");
  auto count = client->GetEventHandles(client.get(), handles.data(), handles.size() - appended_handles);
  if (!count) return 0;
  auto transport_count = count;
  Expects(count <= handles.size() - appended_handles, "transport respects event budget");
  handles[count++] = wake.get();
  handles[count++] = WTSVirtualChannelManagerGetEventHandle(channels);
  if (sound) handles[count++] = sound->Event();
  Ensures(count - transport_count <= appended_handles, "appended events fit reserved budget");
  return count;
}
void Peer::Serve(std::stop_token quit)
{
  Expects(client && wake, "peer owns transport and wake event");
  // WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
  std::array<HANDLE, 32> handles{};
  if (Configure() && client->Initialize(client.get())) {
    while (!quit.stop_requested()) {
      DWORD count, timeout;
      {
        std::scoped_lock lock(owner.session_guard);
        count = EventHandles(handles);
        timeout = Timeout();
      }
      if (!count) break;
      if (WaitForMultipleObjects(count, handles.data(), FALSE, timeout) == WAIT_FAILED
          || quit.stop_requested()) break;
      std::scoped_lock lock(owner.session_guard);
      if (quit.stop_requested()) break;
      if (!client->CheckFileDescriptor(client.get()) || !Channels() || !SoundChannel() || !Drain()) {
        TransportEnded();
        break;
      }
    }
    std::scoped_lock lock(owner.session_guard);
    client->Disconnect(client.get());
  } else owner.Log(SDLRDP_LOG_ERROR, std::format("Peer initialization failed: {}.",
    freerdp_get_last_error_name(freerdp_get_last_error(client->context))));
  owner.Depart(*this);
  finished = true;
  SetEvent(owner.reap.get());
}
void Peer::TransportEnded()
{
  Expects(client && client->context, "transport context exists");
  auto code = freerdp_get_last_error(client->context);
  auto error = freerdp_get_last_error_name(code);
  bool pending;
  {
    std::scoped_lock lock(owner.frame_guard);
    pending = !dirty.empty() || snapshot != nullptr || client->IsWriteBlocked(client.get());
  }
  if (ExpectedDisconnect(code)) owner.Log(SDLRDP_LOG_INFO, std::format("Peer disconnected: {}.", error));
  else if (active && pending)
    owner.Log(SDLRDP_LOG_ERROR, std::format("Peer transport failed with pending data: {}.", error));
  else if (!activated)
    owner.Log(SDLRDP_LOG_INFO, std::format("Connection closed before activation. {}", error));
}
BOOL Peer::Activate(freerdp_peer* client)
{
  Expects(client && client->context, "peer context exists");
  auto& self = Held(client);
  if (self.active.load()) { self.resizing = false; SetEvent(self.wake.get()); return TRUE; }
  if (!SendCookie(client->context)) return FALSE;
  if (!self.encoder.Select(client->context->settings, self.owner.codec.load())) return FALSE;
  auto event = Connected(client->context->settings);
  event.connected.codec = self.encoder.codec;
  event.connected.screen_width = self.screen_width;
  event.connected.screen_height = self.screen_height;
  event.connected.refresh_millihertz = self.refresh;
  self.ack_enabled = freerdp_settings_get_uint32(client->context->settings, FreeRDP_FrameAcknowledge) != 0;
  self.desktop = {0, 0, int(event.connected.width), int(event.connected.height)};
  self.owner.Takeover(self, event);
  return TRUE;
}
void Peer::Post(sdlrdp_rect area)
{
  dirty.Add(area);
  SetEvent(wake.get());
}
bool Peer::Drain()
{
  Expects(client && client->context, "peer context exists");
  if (!active) return true;
  if (client->DrainOutputBuffer(client.get()) < 0) return false;
  if (client->IsWriteBlocked(client.get())) {
    std::scoped_lock lock(owner.frame_guard);
    ResetEvent(wake.get());
    return true;
  }
  {
    std::scoped_lock lock(owner.frame_guard);
    ResetEvent(wake.get());
    if (suppressed || !Pacing()) return true;
  }
  if (!SendPointer()) return false;
  if (!snapshot && !BeginFrame()) return false;
  return !snapshot || resizing || SendFrame(*this);
}
BOOL Peer::Capabilities(freerdp_peer* client)
{
  auto& self = Held(client);
  auto settings = client->context->settings;
  std::scoped_lock lock(self.owner.frame_guard);
  if (!self.activated) {
    self.screen_width = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
    self.screen_height = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
  }
  auto depth = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
  if (depth != 16 && depth != 24 && depth != 32) {
    self.owner.Log(SDLRDP_LOG_WARN, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return FALSE;
  }
  auto picture = self.resizing ? self.desktop : self.owner.Picture();
  return freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w)
    && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h);
}
bool Peer::BeginFrame()
{
  sdlrdp_rect picture;
  {
    std::scoped_lock lock(owner.frame_guard);
    if (dirty.empty() || !owner.shadow) return true;
    picture = owner.Picture();
    snapshot = owner.shadow;
    snapshot_width = owner.frame_width; snapshot_height = owner.frame_height;
    sequence = owner.presented;
    sending = std::move(dirty);
    dirty = {};
    rect_index = row = 0;
  }
  if (picture.w != desktop.w || picture.h != desktop.h) {
    desktop = picture;
    resizing = true;
    auto settings = client->context->settings;
    if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w)
        || !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h)
        || !client->context->update->DesktopResize(client->context)) return false;
    sending.clear();
    sending.Add({0, 0, int(snapshot_width), int(snapshot_height)});
  }
  ++frame_id;
  frame_started = false;
  return true;
}
}
