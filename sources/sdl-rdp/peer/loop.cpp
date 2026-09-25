#include <sdl-rdp/peer/loop.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/waitable.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/peer/departure.hpp>
#include <sdl-rdp/peer/exceptions.hpp>
#include <sdl-rdp/peer/pump.hpp>
#include <sdl-rdp/peer/wait.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/scoped.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>

#include <freerdp/settings.h>
#include <winpr/synch.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <string_view>
#include <tuple>
#include <utility>

namespace sdl_rdp::peer::detail::loop {
using sdl_rdp::diagnostics::PeerNegotiationLogging;
using sdl_rdp::diagnostics::ResetAuthenticationLogging;
using sdl_rdp::freerdp_facade::EventWaitFailed;
using sdl_rdp::freerdp_facade::Waitable;
using sdl_rdp::picture::ApplyDesktopSize;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RAIIWrap;
using sdl_rdp::video::AcknowledgedFrameWindow;

namespace {
using sdl_rdp::freerdp_facade::FirstRefused;
using sdl_rdp::freerdp_facade::Set;
using SecurityFlags = std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 12>;
auto Flags(sdlrdp_auth auth) -> SecurityFlags {
  return { {
      { FreeRDP_NlaSecurity, auth == SDLRDP_AUTH_NLA },
      // sdl-rdp#41: FreeRDP 3.32 nla.c:943 sends Early User Authorization success before Logon decides.
      { FreeRDP_ExtSecurity              , false                    },
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
  std::array<std::pair<FreeRDP_Settings_Keys_UInt32, std::uint32_t>, 3> const numbers{ {
      { FreeRDP_EncryptionLevel , ENCRYPTION_LEVEL_CLIENT_COMPATIBLE                    },
      { FreeRDP_FrameAcknowledge, AcknowledgedFrameWindow                               },
      { FreeRDP_LargePointerFlag, LARGE_POINTER_FLAG_96x96 | LARGE_POINTER_FLAG_384x384 },
  } };
  return Set(settings, FreeRDP_AuthenticationPackageList, "!kerberos") && !FirstRefused(settings, Flags(auth))
         && !FirstRefused(settings, numbers) && ApplyDesktopSize(settings, picture);
}
auto BeginNegotiationLogging(rdpSettings& settings) -> rdpSettings& {
  ResetAuthenticationLogging();
  PeerNegotiationLogging(settings);
  return settings;
}
auto EndNegotiationLogging(rdpSettings& /*settings*/) noexcept -> void {
  ResetAuthenticationLogging();
}
using NegotiationLogging = RAIIWrap<rdpSettings&, BeginNegotiationLogging, EndNegotiationLogging>;
struct LiveConnection {
  std::reference_wrapper<PeerLink>      link;
  std::reference_wrapper<SessionAccess> session;
};
auto Connect(PeerLink& link, SessionAccess& session) -> LiveConnection {
  auto& client = link.Client();
  if (!client.Initialize(&client)) throw PeerSetupFailed{ "initialization" };
  return { .link = link, .session = session };
}
auto Disconnect(LiveConnection const& live) noexcept -> void {
  auto const held   = live.session.get().Lock();
  auto&      client = live.link.get().Client();
  client.Disconnect(&client);
}
using Connection = RAIIWrap<LiveConnection, Connect, Disconnect>;
}
PeerLoop::PeerLoop(PeerLink& link, SessionAccess& session, Diagnostics const& diagnostics,
                   Authenticator const& authenticator, FrameStore& store, PeerWait& wait, PeerPump& pump,
                   Departure& departure) noexcept
    : _link{ link }, _session{ session }, _diagnostics{ diagnostics }, _authenticator{ authenticator }, _store{ store },
      _wait{ wait }, _pump{ pump }, _departure{ departure } { }
auto PeerLoop::Start() -> void {
  Expects(!_thread.joinable(), "peer starts once");
  _thread = std::jthread([this](std::stop_token const& quit) { Serve(quit); });
}
auto PeerLoop::Stop() -> void {
  _thread.request_stop();
}
auto PeerLoop::Serve(std::stop_token const& quit) -> void {
  std::stop_callback const wake(quit, [this] { _link.Signal(); });
  NegotiationLogging const logging { _link.Settings() };
  auto const               served  = [&] {
    if (!Configure()) throw PeerSetupFailed{ "configuration" };
    Run(quit);
    return true;
  };
  auto const               failed  = [this](std::string_view failure) {
    _diagnostics.Log(
        SDLRDP_LOG_ERROR,
        std::format("{} FreeRDP: {}.", failure, freerdp_get_last_error_name(freerdp_get_last_error(&_link.Context()))));
  };
  std::ignore = Contained(false, served, failed);
  _departure.Depart();
}
auto PeerLoop::Run(std::stop_token const& quit) -> void {
  Connection const                             connection{ _link, _session };
  std::array<WaitHandle, MAXIMUM_WAIT_OBJECTS> handles   { };
  while (!quit.stop_requested() && Step(quit, handles)) {
  }
}
auto PeerLoop::Configure() -> bool {
  auto const picture  = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Picture(held); });
  auto&      settings = _link.Settings();
  _authenticator.InstallCredentials(settings);
  return ApplySettings(settings, _authenticator.Auth(), picture);
}
auto PeerLoop::Step(std::stop_token const& quit, std::span<WaitHandle> handles) -> bool {
  auto const plan = [&] {
    auto const session = _session.Lock();
    return _wait.Plan(handles);
  }();
  return plan.count && Dispatch(quit, handles.first(plan.count), plan.timeout);
}
auto PeerLoop::Dispatch(std::stop_token const& quit, std::span<WaitHandle> handles, std::uint32_t timeout) -> bool {
  auto const result = WaitForMultipleObjects(Narrowed<std::uint32_t>(handles.size()), handles.data(), false, timeout);
  if (result == WAIT_FAILED || quit.stop_requested()) return false;
  std::array<WaitHandle, MAXIMUM_WAIT_OBJECTS> signalled{ };
  WaitHandle*                                  end      { };
  try {
    end = std::ranges::copy_if(handles, signalled.begin(), [](WaitHandle handle) {
            return Waitable{ handle }.Signalled();
          }).out;
  } catch (EventWaitFailed const&) {
    return false;
  }
  if (result < handles.size()) std::ranges::rotate(handles, handles.begin() + result + 1);
  return _pump.Service(quit, { signalled.begin(), end });
}
}
