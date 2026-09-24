#include "_detail/peer-loop.hpp"

#include "_detail/acknowledgement-window.hpp"
#include "_detail/configuration.hpp"
#include "_detail/departure.hpp"
#include "_detail/desktop-layout.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/frame-store.hpp"
#include "_detail/logging.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/peer-pump.hpp"
#include "_detail/peer-wait.hpp"
#include "_detail/session-access.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <freerdp/settings.h>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <winpr/synch.h>

namespace Backend {
namespace {
using SecurityFlags = std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 11>;
auto Apply(rdpSettings& settings, std::ranges::input_range auto const& entries, auto set) -> bool {
  return std::ranges::all_of(entries, [&](auto const& entry) { return set(&settings, entry.first, entry.second); });
}
auto Flags(sdlrdp_auth auth) -> SecurityFlags {
  return { {
    { FreeRDP_NlaSecurity              , auth == SDLRDP_AUTH_NLA  },
    { FreeRDP_TlsSecurity              , true                     },
    { FreeRDP_RdpSecurity              , auth == SDLRDP_AUTH_NONE },
    { FreeRDP_RemoteFxCodec            , true                     },
    { FreeRDP_NSCodec                  , true                     },
    { FreeRDP_SupportGraphicsPipeline  , true                     },
    { FreeRDP_AutoReconnectionEnabled  , true                     },
    { FreeRDP_WaitForOutputBufferFlush , false                    },
    { FreeRDP_FrameMarkerCommandEnabled, true                     },
    { FreeRDP_SupportDisplayControl    , true                     },
    { FreeRDP_SuppressOutput           , true                     },
  } };
}
auto ApplySettings(rdpSettings& settings, sdlrdp_auth auth, sdlrdp_rect picture) -> bool {
  std::array const numbers{
    std::pair{ FreeRDP_EncryptionLevel, UINT32(ENCRYPTION_LEVEL_CLIENT_COMPATIBLE) },
    std::pair{ FreeRDP_FrameAcknowledge, UINT32(AcknowledgedFrameWindow) },
    std::pair{ FreeRDP_LargePointerFlag, UINT32(LARGE_POINTER_FLAG_96x96 | LARGE_POINTER_FLAG_384x384) },
  };
  return freerdp_settings_set_string(&settings, FreeRDP_AuthenticationPackageList, "!kerberos") &&
         Apply(settings, Flags(auth), freerdp_settings_set_bool) &&
         Apply(settings, numbers, freerdp_settings_set_uint32) && ApplyDesktopSize(settings, picture);
}
auto NegotiationLogging(rdpSettings& settings) {
  ResetAuthenticationLogging();
  PeerNegotiationLogging(&settings);
  return std::unique_ptr<rdpSettings, decltype([](rdpSettings*) { ResetAuthenticationLogging(); })>{ &settings };
}
auto Connection(PeerLink& link, SessionAccess& session) {
  auto  disconnect = [&session](freerdp_peer* client) {
    auto const held = session.Lock();
    client->Disconnect(client);
  };
  auto& client     = link.Client();
  return std::unique_ptr<freerdp_peer, decltype(disconnect)>{ client.Initialize(&client) ? &client : nullptr,
                                                              disconnect };
}
}
PeerLoop::PeerLoop(PeerLink& link, SessionAccess& session, Diagnostics const& diagnostics,
                   Configuration const& configuration, FrameStore& store, PeerWait& wait, PeerPump& pump,
                   Departure& departure) noexcept
    : _link { link }, _session{ session }, _diagnostics{ diagnostics }, _configuration{ configuration },
      _store{ store }, _wait{ wait }, _pump{ pump }, _departure{ departure } { }
auto PeerLoop::Start() -> void {
  Expects(!_thread.joinable(), "peer starts once");
  _thread = std::jthread([this](std::stop_token const& quit) { Serve(quit); });
}
auto PeerLoop::Stop() -> void {
  _thread.request_stop();
}
auto PeerLoop::Serve(std::stop_token const& quit) -> void {
  std::stop_callback const wake(quit, [this] { _link.Signal(); });
  auto const               logging = NegotiationLogging(_link.Settings());
  if (!Configure() || !Run(quit))
    _diagnostics.Log(SDLRDP_LOG_ERROR,
                     std::format("Peer initialization failed: {}.",
                                 freerdp_get_last_error_name(freerdp_get_last_error(&_link.Context()))));
  _departure.Depart();
}
auto PeerLoop::Run(std::stop_token const& quit) -> bool {
  auto const connection = Connection(_link, _session);
  if (!connection) return false;
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> handles{ };
  while (!quit.stop_requested() && Step(quit, handles)) {
  }
  return true;
}
auto PeerLoop::Configure() -> bool {
  auto const picture  = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Picture(held); });
  auto&      settings = _link.Settings();
  return _configuration.InstallCredentials(settings) && ApplySettings(settings, _configuration.Auth(), picture);
}
auto PeerLoop::Step(std::stop_token const& quit, std::span<HANDLE> handles) -> bool {
  auto const plan = [&] {
    auto const session = _session.Lock();
    return _wait.Plan(handles);
  }();
  return plan.count && Dispatch(quit, handles.first(plan.count), plan.timeout);
}
auto PeerLoop::Dispatch(std::stop_token const& quit, std::span<HANDLE> handles, DWORD timeout) -> bool {
  auto const result = WaitForMultipleObjects(DWORD(handles.size()), handles.data(), FALSE, timeout);
  if (result == WAIT_FAILED || quit.stop_requested()) return false;
  std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> signalled{ };
  HANDLE*                                  end      { };
  try {
    end = std::ranges::copy_if(handles, signalled.begin(), Signalled).out;
  } catch (std::runtime_error const&) {
    return false;
  }
  if (result < handles.size()) std::ranges::rotate(handles, handles.begin() + result + 1);
  return _pump.Service(quit, { signalled.begin(), end });
}
}
