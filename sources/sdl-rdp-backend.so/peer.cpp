#include "_detail/input.hpp"
#include "_detail/state.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <freerdp/input.h>
#include <freerdp/session.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <stdexcept>
#include <utility>
#include <winpr/crypto.h>
#include <winpr/synch.h>

namespace Backend {
namespace {
std::string ProtocolNames(UINT32 mask, bool rdp) {
  std::string names = rdp ? "RDP" : "";
  for (auto [flag, name] : std::array<std::pair<UINT32, char const*>, 5>{ { { SecurityTls, "TLS" },
                                                                            { SecurityNla, "NLA" },
                                                                            { SecurityNlaExt, "NLA_EXT" },
                                                                            { SecurityRdstls, "RDSTLS" },
                                                                            { SecurityRdsaad, "RDSAAD" } } }) {
    if (!(mask & flag)) continue;
    if (!names.empty()) names += '|';
    names += name;
  }
  return names;
}
bool SecurityEnded(Peer& peer) {
  Expects(peer.client != nullptr, "peer owns its transport");
  Expects(peer.client->context, "peer transport has a context");
  if (!NegotiationRefused() && !TlsHandshakeFailed()) return false;
  auto* settings = peer.client->context->settings;
  // FreeRDP 3.15 nego.c publishes requestedProtocols even after negotiation fails.
  auto requested = freerdp_settings_get_uint32(settings, FreeRDP_RequestedProtocols);
  auto protocols = ProtocolNames(requested, !requested);
  if (NegotiationRefused()) {
    auto offered = (freerdp_settings_get_bool(settings, FreeRDP_TlsSecurity) ? SecurityTls : 0) |
                   (freerdp_settings_get_bool(settings, FreeRDP_NlaSecurity) ? SecurityNla : 0);
    peer.owner.Log(SDLRDP_LOG_WARN,
                   std::format("Connection refused: client requested {}, server offers {}", protocols,
                               ProtocolNames(offered, freerdp_settings_get_bool(settings, FreeRDP_RdpSecurity))));
  } else {
    auto selected = freerdp_settings_get_uint32(settings, FreeRDP_SelectedProtocol);
    peer.owner.Log(SDLRDP_LOG_WARN, std::format("TLS handshake failed: client requested {}, server selected {}",
                                                protocols, ProtocolNames(selected, !selected)));
  }
  return true;
}
sdlrdp_event Connected(rdpSettings const* settings) {
  Expects(settings != nullptr, "settings exist");
  sdlrdp_event event{ .type = SDLRDP_CONNECTED };
  event.connected.width           = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
  event.connected.height          = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
  event.connected.keyboard_layout = freerdp_settings_get_uint32(settings, FreeRDP_KeyboardLayout);
  event.connected.bpp             = freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth);
  auto const* name                = freerdp_settings_get_string(settings, FreeRDP_ClientHostname);
  if (name) std::strncpy(event.connected.client_name, name, sizeof(event.connected.client_name) - 1);
  return event;
}
bool SendCookie(rdpContext* context) {
  Expects(context != nullptr, "session context exists");
  ARC_SC_PRIVATE_PACKET cookie{};
  cookie.cbLen   = 28;
  cookie.version = AUTO_RECONNECT_VERSION_1;
  cookie.logonId = 1;
  if (winpr_RAND(cookie.arcRandomBits, sizeof(cookie.arcRandomBits)) != 0) return false;
  if (!freerdp_settings_set_pointer_len(context->settings, FreeRDP_ServerAutoReconnectCookie, &cookie, 1)) return false;
  logon_info_ex info{};
  info.haveCookie = TRUE;
  info.LogonId    = cookie.logonId;
  std::ranges::copy(cookie.arcRandomBits, info.ArcRandomBits);
  return context->update->SaveSessionInfo(context, INFO_TYPE_LOGON_EXTENDED_INF, &info);
}
}
namespace {
bool InstallCredentials(rdpSettings* settings, std::string const& key_path, std::string const& certificate_path) {
  std::unique_ptr<rdpPrivateKey, Releases<freerdp_key_free>> key(freerdp_key_new_from_file(key_path.c_str()));
  std::unique_ptr<rdpCertificate, Releases<freerdp_certificate_free>> cert(
      freerdp_certificate_new_from_file(certificate_path.c_str()));
  if (!key || !cert) return false;
  // These two pointer setters transfer ownership despite the generic API's copy documentation.
  if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key.get(), 1)) return false;
  std::ignore = key.release();
  if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert.get(), 1)) return false;
  std::ignore = cert.release();
  return true;
}
void ReportDisconnect(Peer& peer, UINT32 code, char const* error, bool pending) {
  if (ExpectedDisconnect(code))
    peer.owner.Log(SDLRDP_LOG_INFO, peer.activated ? std::format("Peer disconnected: {}.", error)
                                                   : std::format("Connection closed before activation: {}.", error));
  else if (peer.active && pending)
    peer.owner.Log(SDLRDP_LOG_ERROR, std::format("Peer transport failed with pending data: {}.", error));
  else if (!peer.activated)
    peer.owner.Log(SDLRDP_LOG_INFO, code ? std::format("Connection closed before activation: {}.", error)
                                         : "Connection closed before activation.");
}
} // namespace
void Peer::InstallCallbacks() const {
  auto* raw = client.get();
  raw->context->update->SurfaceFrameAcknowledge = Acknowledge;
  raw->context->update->SuppressOutput          = Suppress;
  auto* input = raw->context->input;
  input->KeyboardEvent                          = Keyboard;
  input->UnicodeKeyboardEvent                   = Input::Unicode;
  input->MouseEvent                             = Mouse;
  input->ExtendedMouseEvent                     = ExtendedMouse;
}
std::pair<DWORD, DWORD> Peer::PollParameters(std::span<HANDLE> handles) {
  DWORD count   = 0;
  DWORD timeout = 0;
  {
    std::scoped_lock const lock(owner.session_guard);
    GraphicsDeadline();
    if (!handle_count || !activated) handle_count = EventHandles(handles);
    count   = handle_count;
    timeout = Timeout();
  }
  return { count, timeout };
}
BOOL Peer::TakeControl() {
  auto event = Connected(client->context->settings);
  AuthenticationIdentity(client.get(), event);
  event.connected.refresh_millihertz = effective_refresh.load() * 1000;
  event.connected.codec              = encoder.codec;
  event.connected.screen_width       = screen_width;
  event.connected.screen_height      = screen_height;
  ack_enabled = freerdp_settings_get_uint32(client->context->settings, FreeRDP_FrameAcknowledge) != 0;
  desktop     = { .x = 0, .y = 0, .w = int(event.connected.width), .h = int(event.connected.height) };
  owner.Takeover(*this, event);
  owner.trace.Line("connect", [&] { return std::format("client={}", event.connected.client_name); });
  return TRUE;
}
Peer& Peer::Held(freerdp_peer* client) {
  Expects(client != nullptr, "client transport exists");
  Expects(client->ContextExtra, "client carries its owner");
  return *static_cast<Peer*>(client->ContextExtra);
}
Peer::Peer(PeerHandle accepted, State& state)
    : client(std::move(accepted)), owner(state), wake(CreateEvent(nullptr, TRUE, FALSE, nullptr)) {
  auto* raw = client.get();
  Expects(raw != nullptr, "accepted peer exists");
  if (!wake) throw std::runtime_error("peer event allocation failed");
  raw->ContextSize          = sizeof(InputContext);
  raw->ContextNew           = Input::Create;
  raw->ContextFree          = Input::Free;
  raw->ContextExtra         = this;
  raw->Activate             = Activate;
  raw->Logon                = Authenticate;
  raw->SspiNtlmHashCallback = AuthenticationHash;
  raw->Capabilities         = Capabilities;
  raw->PostConnect          = [](freerdp_peer*) -> BOOL { return TRUE; };
  if (!freerdp_peer_context_new(raw)) throw std::runtime_error("peer context failed");
  channels = WTSOpenServerA(reinterpret_cast<char*>(raw->context));
  if (!channels || channels == INVALID_HANDLE_VALUE) throw std::runtime_error("Channel manager allocation failed.");
  WTSVirtualChannelManagerSetDVCCreationCallback(channels, ChannelCreated, this);
  InstallCallbacks();
}
Peer::~Peer() {
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
void Peer::Start() {
  Expects(!thread.joinable(), "peer starts once");
  thread = std::jthread([this](std::stop_token const& quit) { Serve(quit); });
}
bool Peer::Configure() {
  Expects(client != nullptr, "client transport exists");
  Expects(client->context, "client context exists");
  sdlrdp_rect picture;
  {
    std::scoped_lock const lock(owner.frame_guard);
    picture = owner.Picture();
  }
  auto* settings = client->context->settings;
  if (!InstallCredentials(settings, owner.credentials.key, owner.credentials.certificate)) return false;
  return freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos") &&
         freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity,
                                   owner.authentication.Config().auth == SDLRDP_AUTH_NLA) &&
         freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity,
                                   owner.authentication.Config().auth == SDLRDP_AUTH_NONE) &&
         freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_WaitForOutputBufferFlush, FALSE) &&
         freerdp_settings_set_uint32(settings, FreeRDP_EncryptionLevel, ENCRYPTION_LEVEL_CLIENT_COMPATIBLE) &&
         freerdp_settings_set_bool(settings, FreeRDP_FrameMarkerCommandEnabled, TRUE) &&
         freerdp_settings_set_uint32(settings, FreeRDP_FrameAcknowledge, 2) &&
         freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE) &&
         freerdp_settings_set_bool(settings, FreeRDP_SuppressOutput, TRUE) &&
         freerdp_settings_set_uint32(settings, FreeRDP_LargePointerFlag,
                                     LARGE_POINTER_FLAG_96x96 | LARGE_POINTER_FLAG_384x384) &&
         freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, picture.w) &&
         freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, picture.h);
}
DWORD Peer::EventHandles(std::span<HANDLE> handles) {
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
bool Peer::PollStep(std::stop_token const& quit, std::span<HANDLE> handles) {
  Expects(handles.size() <= MAXIMUM_WAIT_OBJECTS, "event array fits the wait and readiness budget");
  auto [count, timeout] = PollParameters(handles);
  if (!count) return false;
  auto result = WaitForMultipleObjects(count, handles.data(), FALSE, timeout);
  if (result == WAIT_FAILED || quit.stop_requested()) return false;
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> signalled{};
  HANDLE* end = nullptr;
  try {
    end = std::ranges::copy_if(handles.first(count), signalled.begin(), Signalled).out;
  } catch (std::runtime_error const&) {
    return false;
  }
  auto ready = std::span<HANDLE const>(signalled.begin(), end);
  if (result < count) std::ranges::rotate(handles.first(count), handles.begin() + result + 1);
  auto healthy = TransportStep(quit, ready);
  std::ranges::for_each(trace_pending, [&](auto const& text) { owner.trace.Emit(text); });
  trace_pending.clear();
  return healthy;
}
void Peer::Serve(std::stop_token const& quit) {
  Expects(client != nullptr, "peer owns its transport");
  Expects(bool(wake), "peer owns its wake event");
  ResetAuthenticationLogging();
  PeerNegotiationLogging(client->context->settings);
  // WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> handles{};
  if (Configure() && client->Initialize(client.get())) {
    while (!quit.stop_requested() && PollStep(quit, handles)) {
    }
    std::scoped_lock const lock(owner.session_guard);
    client->Disconnect(client.get());
  } else
    owner.Log(SDLRDP_LOG_ERROR, std::format("Peer initialization failed: {}.",
                                            freerdp_get_last_error_name(freerdp_get_last_error(client->context))));
  owner.Depart(*this);
  finished = true;
  SetEvent(owner.reap.get());
  ResetAuthenticationLogging();
}
bool Peer::OpenStaticChannels(std::span<HANDLE const> ready) {
  if (!clipboard && WTSVirtualChannelManagerIsChannelJoined(channels, CLIPRDR_SVC_CHANNEL_NAME)) {
    handle_count = 0;
    clipboard    = std::make_unique<ClipboardChannel>(*this);
    if (!clipboard->Open()) return false;
  }
  if (!drive && WTSVirtualChannelManagerIsChannelJoined(channels, "rdpdr")) {
    handle_count = 0;
    drive        = std::make_shared<DriveChannel>(*this);
    drive->Open();
  }
  if (drive) drive->Pump(ready);
  return !clipboard || clipboard->Pump(ready);
}
bool Peer::Channels(std::span<HANDLE const> ready) {
  Expects(client != nullptr, "client transport exists");
  Expects(client->context, "client context exists");
  if (!channels || !active) return true;
  return WTSVirtualChannelManagerCheckFileDescriptor(channels) && Input::Held(*this).Channels(*this, ready) &&
         OpenStaticChannels(ready) && OpenDisplayControl() && GraphicsChannel(ready);
}
void Peer::TransportEnded() {
  Expects(client != nullptr, "client transport exists");
  Expects(client->context, "client context exists");
  if (SecurityEnded(*this)) return;
  AuthenticationEnded();
  auto        code    = freerdp_get_last_error(client->context);
  auto const* error   = freerdp_get_last_error_name(code);
  bool        pending = false;
  {
    std::scoped_lock const lock(owner.frame_guard);
    pending = !dirty.empty() || snapshot != nullptr || client->IsWriteBlocked(client.get());
  }
  ReportDisconnect(*this, code, error, pending);
}
BOOL Peer::Activate(freerdp_peer* client) {
  Expects(client != nullptr, "peer exists");
  Expects(client->context != nullptr, "peer context exists");
  auto& self = Held(client);
  if (self.active.load()) {
    self.wake.Transition(WakeEvent::Phase::Pending);
    return TRUE;
  }
  if (!AuthenticateSettings(client)) return FALSE;
  if (!SendCookie(client->context)) return FALSE;
  if (!self.encoder.Select(client->context->settings, self.owner.codec.load())) return FALSE;
  return self.TakeControl();
}
void Peer::Post(sdlrdp_rect area) {
  dirty.Add(area);
  wake.Transition(WakeEvent::Phase::Pending);
}
}
