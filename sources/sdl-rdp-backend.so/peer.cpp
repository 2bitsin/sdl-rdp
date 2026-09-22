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
  raw->PostConnect = [](freerdp_peer*) -> BOOL { return TRUE; };
  if (!freerdp_peer_context_new(raw)) throw std::runtime_error("peer context failed");
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
}
void Peer::Start()
{
  Expects(!thread.joinable(), "peer starts once");
  thread = std::jthread([this](std::stop_token quit) { Serve(quit); });
}
bool Peer::Configure()
{
  Expects(client && client->context, "peer context exists");
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
    && freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)
    && freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, owner.width)
    && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, owner.height);
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
        count = client->GetEventHandles(client.get(), handles.data(), 31);
        timeout = client->IsWriteBlocked(client.get()) ? 5 : INFINITE;
      }
      if (!count) break;
      handles[count++] = wake.get();
      if (WaitForMultipleObjects(count, handles.data(), FALSE, timeout) == WAIT_FAILED
          || quit.stop_requested()) break;
      std::scoped_lock lock(owner.session_guard);
      if (quit.stop_requested()) break;
      if (!client->CheckFileDescriptor(client.get()) || !Drain()) {
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
  auto error = freerdp_get_last_error_name(freerdp_get_last_error(client->context));
  bool pending;
  {
    std::scoped_lock lock(owner.frame_guard);
    pending = dirty.has_value() || client->IsWriteBlocked(client.get());
  }
  if (active && pending)
    owner.Log(SDLRDP_LOG_ERROR, std::format("Peer transport failed with pending data: {}.", error));
  else if (!activated)
    owner.Log(SDLRDP_LOG_INFO, std::format("Connection closed before activation. {}", error));
}
BOOL Peer::Activate(freerdp_peer* client)
{
  Expects(client && client->context, "peer context exists");
  auto& self = Held(client);
  if (self.active.load()) return TRUE;
  if (!SendCookie(client->context)) return FALSE;
  if (!self.encoder.Select(client->context->settings, self.owner.codec.load())) return FALSE;
  auto event = Connected(client->context->settings);
  event.connected.codec = self.encoder.codec;
  self.desktop = {0, 0, int(event.connected.width), int(event.connected.height)};
  self.owner.Takeover(self, event);
  return TRUE;
}
void Peer::Post(sdlrdp_rect area)
{
  Merge(dirty, area);
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
  std::optional<sdlrdp_rect> region;
  {
    std::scoped_lock lock(owner.frame_guard);
    region = std::exchange(dirty, std::nullopt);
    ResetEvent(wake.get());
  }
  if (region) region = Intersect(*region, desktop);
  if (!region || SendFrame(client->context, owner, *region)) return true;
  std::scoped_lock lock(owner.frame_guard);
  Post(*region);
  return false;
}
}
