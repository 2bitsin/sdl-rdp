#include "_detail/state.hpp"
#include "_detail/input.hpp"
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
std::string ProtocolNames(UINT32 mask, bool rdp)
{
  std::string names = rdp ? "RDP" : "";
  for (auto [flag, name] : std::array<std::pair<UINT32, char const*>, 5>{{
      {SecurityTls, "TLS"}, {SecurityNla, "NLA"}, {SecurityNlaExt, "NLA_EXT"},
      {SecurityRdstls, "RDSTLS"}, {SecurityRdsaad, "RDSAAD"}}}) {
    if (!(mask & flag)) continue;
    if (!names.empty()) names += '|';
    names += name;
  }
  return names;
}
bool SecurityEnded(Peer& peer)
{
  Expects(peer.client && peer.client->context, "security context exists");
  if (!NegotiationRefused() && !TlsHandshakeFailed()) return false;
  auto settings = peer.client->context->settings;
  // FreeRDP 3.15 nego.c publishes requestedProtocols even after negotiation fails.
  auto requested = freerdp_settings_get_uint32(settings, FreeRDP_RequestedProtocols);
  auto protocols = ProtocolNames(requested, !requested);
  if (NegotiationRefused()) {
    auto offered = (freerdp_settings_get_bool(settings, FreeRDP_TlsSecurity) ? SecurityTls : 0)
      | (freerdp_settings_get_bool(settings, FreeRDP_NlaSecurity) ? SecurityNla : 0);
    peer.owner.Log(SDLRDP_LOG_WARN, std::format("Connection refused: client requested {}, server offers {}",
      protocols, ProtocolNames(offered, freerdp_settings_get_bool(settings, FreeRDP_RdpSecurity))));
  } else {
    auto selected = freerdp_settings_get_uint32(settings, FreeRDP_SelectedProtocol);
    peer.owner.Log(SDLRDP_LOG_WARN, std::format("TLS handshake failed: client requested {}, server selected {}",
      protocols, ProtocolNames(selected, !selected)));
  }
  return true;
}
sdlrdp_event Connected(rdpSettings const* settings)
{
  Expects(settings != nullptr, "settings exist");
  sdlrdp_event event{.type = SDLRDP_CONNECTED};
  event.connected.width = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
  event.connected.height = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
  event.connected.keyboard_layout = freerdp_settings_get_uint32(settings, FreeRDP_KeyboardLayout);
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
  raw->ContextSize = sizeof(InputContext);
  raw->ContextNew = Input::Create;
  raw->ContextFree = Input::Free;
  raw->ContextExtra = this;
  raw->Activate = Activate;
  raw->Logon = Authenticate;
  raw->SspiNtlmHashCallback = AuthenticationHash;
  raw->Capabilities = Capabilities;
  raw->PostConnect = [](freerdp_peer*) -> BOOL { return TRUE; };
  if (!freerdp_peer_context_new(raw)) throw std::runtime_error("peer context failed");
  channels = WTSOpenServerA(reinterpret_cast<char*>(raw->context));
  if (!channels || channels == INVALID_HANDLE_VALUE) throw std::runtime_error("Channel manager allocation failed.");
  WTSVirtualChannelManagerSetDVCCreationCallback(channels, ChannelCreated, this);
  raw->context->update->SurfaceFrameAcknowledge = Acknowledge;
  raw->context->update->SuppressOutput = Suppress;
  auto input = raw->context->input;
  input->KeyboardEvent = Keyboard;
  input->UnicodeKeyboardEvent = Input::Unicode;
  input->MouseEvent = Mouse;
  input->ExtendedMouseEvent = ExtendedMouse;
}
Peer::~Peer()
{
  thread.request_stop();
  wake.Transition(WakeEvent::Phase::Pending);
  if (thread.joinable()) thread.join();
  Input::Held(*this).Close();
  if (drive) drive->Disconnect();
  drive.reset();
  clipboard.reset();
  sound.reset();
  disp.reset();
  gfx.reset();
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
  return freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos")
    && freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, owner.authentication.config.auth == SDLRDP_AUTH_NLA)
    && freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, owner.authentication.config.auth == SDLRDP_AUTH_NONE)
    && freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE)
    && freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE)
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
  Expects(handles.size() > AppendedHandleCount, "event array has room for transport and peer handles");
  auto count = client->GetEventHandles(client.get(), handles.data(), handles.size() - AppendedHandleCount);
  if (!count) return 0;
  auto transport_count = count;
  Expects(count <= handles.size() - AppendedHandleCount, "transport respects event budget");
  count += Input::Held(*this).Handles(handles.data() + count);
  if (drive && drive->Event()) handles[count++] = drive->Event();
  if (clipboard) handles[count++] = clipboard->Event();
  if (sound) handles[count++] = sound->Event();
  if (gfx) handles[count++] = gfx->Event();
  handles[count++] = wake.get();
  handles[count++] = WTSVirtualChannelManagerGetEventHandle(channels);
  Ensures(count - transport_count <= AppendedHandleCount, "appended events fit reserved budget");
  return count;
}
bool Peer::PollStep(std::stop_token quit, std::span<HANDLE> handles)
{
  Expects(handles.size() <= MAXIMUM_WAIT_OBJECTS, "event array fits the wait and readiness budget");
  DWORD count, timeout;
  {
    std::scoped_lock lock(owner.session_guard);
    GraphicsDeadline();
    if (!handle_count || !activated) handle_count = EventHandles(handles);
    count = handle_count;
    timeout = Timeout();
  }
  if (!count) return false;
  auto result = WaitForMultipleObjects(count, handles.data(), FALSE, timeout);
  if (result == WAIT_FAILED || quit.stop_requested()) return false;
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> signalled{};
  auto end = signalled.begin();
  try {
    end = std::ranges::copy_if(handles.first(count), signalled.begin(), Signalled).out;
  } catch (std::runtime_error const&) { return false; }
  auto ready = std::span<HANDLE const>(signalled.begin(), end);
  if (result < count) std::ranges::rotate(handles.first(count), handles.begin() + result + 1);
  auto healthy = TransportStep(quit, ready);
  std::ranges::for_each(trace_pending, [&](auto const& text) { owner.trace.Emit(text); });
  trace_pending.clear();
  return healthy;
}
void Peer::Serve(std::stop_token quit)
{
  Expects(client && wake, "peer owns transport and wake event");
  ResetAuthenticationLogging();
  PeerNegotiationLogging(client->context->settings);
  // WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> handles{};
  if (Configure() && client->Initialize(client.get())) {
    while (!quit.stop_requested()) {
      if (!PollStep(quit, handles)) break;
    }
    std::scoped_lock lock(owner.session_guard);
    client->Disconnect(client.get());
  } else owner.Log(SDLRDP_LOG_ERROR, std::format("Peer initialization failed: {}.",
    freerdp_get_last_error_name(freerdp_get_last_error(client->context))));
  owner.Depart(*this);
  finished = true;
  SetEvent(owner.reap.get());
  ResetAuthenticationLogging();
}
bool Peer::OpenStaticChannels(std::span<HANDLE const> ready)
{
  if (!clipboard && WTSVirtualChannelManagerIsChannelJoined(channels, CLIPRDR_SVC_CHANNEL_NAME)) {
    handle_count = 0;
    clipboard = std::make_unique<ClipboardChannel>(*this);
    if (!clipboard->Open()) return false;
  }
  if (!drive && WTSVirtualChannelManagerIsChannelJoined(channels, "rdpdr")) {
    handle_count = 0;
    drive = std::make_shared<DriveChannel>(*this);
    drive->Open();
  }
  if (drive) drive->Pump(ready);
  return !clipboard || clipboard->Pump(ready);
}
bool Peer::Channels(std::span<HANDLE const> ready)
{
  Expects(client && client->context, "channel peer exists");
  if (!channels || !active) return true;
  return WTSVirtualChannelManagerCheckFileDescriptor(channels)
    && Input::Held(*this).Channels(*this, ready) && OpenStaticChannels(ready) && OpenDisplayControl() && GraphicsChannel(ready);
}
void Peer::TransportEnded()
{
  Expects(client && client->context, "transport context exists");
  if (SecurityEnded(*this)) return;
  AuthenticationEnded();
  auto code = freerdp_get_last_error(client->context);
  auto error = freerdp_get_last_error_name(code);
  bool pending;
  {
    std::scoped_lock lock(owner.frame_guard);
    pending = !dirty.empty() || snapshot != nullptr || client->IsWriteBlocked(client.get());
  }
  if (ExpectedDisconnect(code)) owner.Log(SDLRDP_LOG_INFO, activated
    ? std::format("Peer disconnected: {}.", error)
    : std::format("Connection closed before activation: {}.", error));
  else if (active && pending)
    owner.Log(SDLRDP_LOG_ERROR, std::format("Peer transport failed with pending data: {}.", error));
  else if (!activated)
    owner.Log(SDLRDP_LOG_INFO, code
      ? std::format("Connection closed before activation: {}.", error)
      : "Connection closed before activation.");
}
BOOL Peer::Activate(freerdp_peer* client)
{
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  auto& self = Held(client);
  if (self.active.load()) { self.wake.Transition(WakeEvent::Phase::Pending); return TRUE; }
  if (!AuthenticateSettings(client)) return FALSE;
  if (!SendCookie(client->context)) return FALSE;
  if (!self.encoder.Select(client->context->settings, self.owner.codec.load())) return FALSE;
  auto event = Connected(client->context->settings);
  AuthenticationIdentity(client, event);
  event.connected.codec = self.encoder.codec;
  event.connected.screen_width = self.screen_width;
  event.connected.screen_height = self.screen_height;
  event.connected.refresh_millihertz = self.effective_refresh.load() * 1000;
  self.ack_enabled = freerdp_settings_get_uint32(client->context->settings, FreeRDP_FrameAcknowledge) != 0;
  self.desktop = {0, 0, int(event.connected.width), int(event.connected.height)};
  self.owner.Takeover(self, event);
  self.owner.trace.Line("connect", [&] { return std::format("client={}", event.connected.client_name); });
  return TRUE;
}
void Peer::Post(sdlrdp_rect area)
{
  dirty.Add(area);
  wake.Transition(WakeEvent::Phase::Pending);
}
bool Peer::TransportStep(std::stop_token quit, std::span<HANDLE const> ready)
{
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  {
    std::scoped_lock lock(owner.session_guard);
    if (quit.stop_requested()) return false;
    if (!client->CheckFileDescriptor(client.get()) || !Channels(ready) || !SoundChannel(ready) || !Drain()) {
      TransportEnded();
      return false;
    }
  }
  return EncodeAndSend(quit);
}
void Peer::TransitionEncode(EncodeState next)
{
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
    default: utilities::Unreachable(encode_state);
  }
  encode_state = next;
}
bool Peer::EncodeAndSend(std::stop_token quit)
{
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  bool encoded;
  switch (encode_state) {
    case EncodeState::Idle:
    case EncodeState::LegacyReady: return true;
    case EncodeState::Legacy: encoded = legacy.Encode(*this); break;
    case EncodeState::Graphics:
      Expects(gfx != nullptr, "graphics channel exists");
      Expects(snapshot != nullptr, "immutable frame exists");
      encoded = gfx->Encode();
      break;
    default: utilities::Unreachable(encode_state);
  }
  std::scoped_lock lock(owner.session_guard);
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
bool Peer::Drain()
{
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
bool Peer::ReadyFrame()
{
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  std::scoped_lock lock(owner.frame_guard);
  wake.Transition(WakeEvent::Phase::Idle);
  if (client->IsWriteBlocked(client.get())) {
    auto previous = refresh.rate;
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
      dirty.Add({0, 0, int(owner.frame_width), int(owner.frame_height)});
    }
  }
  if (refresh.awaiting_empty) {
    auto previous = refresh.rate;
    refresh.Drained(SampleWire(socket_descriptor));
    PublishRefresh(previous);
  }
  return Pacing() && !suppressed;
}
bool Peer::PrepareFrame()
{
  if (encode_state == EncodeState::LegacyReady) {
    if (!legacy.Send(*this)) return false;
    if (!snapshot) TransitionEncode(EncodeState::Idle);
    return true;
  }
  if (!(Graphics() ? gfx->Prepare() : legacy.Prepare(*this))) return false;
  TransitionEncode(Graphics() ? EncodeState::Graphics : EncodeState::Legacy);
  return true;
}
BOOL Peer::Capabilities(freerdp_peer* client)
{
  Expects(client != nullptr, "peer exists");
  auto& self = Held(client);
  if (!AuthenticateSettings(client)) return FALSE;
  auto settings = client->context->settings;
  std::scoped_lock lock(self.owner.frame_guard);
  if (!self.activated) {
    self.RestartRefresh();
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
sdlrdp_rect Peer::CaptureFrame()
{
  Expects(!resizing, "no resize in flight");
  std::scoped_lock lock(owner.frame_guard);
  if (dirty.empty() || !owner.shadow) return desktop;
  auto picture = owner.Picture();
  if (picture.w != desktop.w || picture.h != desktop.h ||
      snapshot_width != owner.frame_width || snapshot_height != owner.frame_height) RestartRefresh();
  snapshot = owner.shadow;
  snapshot_width = owner.frame_width; snapshot_height = owner.frame_height;
  sequence = owner.presented;
  frames_coalesced += dirty_presents ? dirty_presents - 1 : 0;
  dirty_presents = 0;
  encoded_at_start = encoder.encode_time;
  sending.rects.swap(dirty.rects);
  dirty.clear();
  return picture;
}
bool Peer::ResizeDesktop(sdlrdp_rect picture)
{
  Expects(client != nullptr, "peer exists");
  Expects(snapshot != nullptr, "resize has a frame");
  desktop = picture;
  resizing = true;
  auto settings = client->context->settings;
  if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w)
      || !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h)
      || !client->context->update->DesktopResize(client->context)) return false;
  Ensures(resizing, "desktop resize remains in flight until the client is active");
  sending.clear();
  sending.Add({0, 0, int(snapshot_width), int(snapshot_height)});
  return true;
}
bool Peer::BeginFrame()
{
  Expects(!resizing, "no resize in flight");
  Expects(freerdp_is_active_state(client->context), "resize requires an active client");
  auto picture = CaptureFrame();
  if (!snapshot) return true;
  if (picture.w != desktop.w || picture.h != desktop.h)
    if (!ResizeDesktop(picture)) return false;
  ++frame_id;
  return true;
}
}
