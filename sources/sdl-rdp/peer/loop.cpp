#include <sdl-rdp/peer/peer.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/logging.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/peer/exceptions.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/scoped.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::diagnostics::PeerNegotiationLogging;
using sdl_rdp::diagnostics::ResetAuthenticationLogging;
using sdl_rdp::freerdp_facade::MaximumWaitHandles;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::picture::ApplyDesktopSize;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RAIIWrap;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::video::AcknowledgedFrameWindow;
using sdl_rdp::video::GraphicsConnectionWait;
using sdl_rdp::video::WaitMilliseconds;
using sdl_rdp::video::frame::Delivery;

namespace {
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::EncryptionLevel;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::SettingsReader;
using sdl_rdp::freerdp_facade::SettingsView;
using sdl_rdp::freerdp_facade::StringKey;
constexpr std::uint32_t LoopHandleCount     = 2;
constexpr std::uint32_t AppendedHandleCount = ChannelHandleLimit + LoopHandleCount;
// WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
constexpr std::uint32_t BlockedRetry = 5;
using SecurityFlags = std::array<std::pair<BoolKey, bool>, 12>;
auto Flags(AuthMode auth) -> SecurityFlags {
  return { {
      { BoolKey::NlaSecurity, auth == AuthMode::Nla },
      // sdl-rdp#41: FreeRDP 3.32 nla.c:943 sends Early User Authorization success before Logon decides.
      { BoolKey::ExtSecurity              , false                  },
      { BoolKey::TlsSecurity              , true                   },
      { BoolKey::RdpSecurity              , auth == AuthMode::None },
      { BoolKey::RemoteFxCodec            , true                   },
      { BoolKey::NSCodec                  , true                   },
      { BoolKey::SupportGraphicsPipeline  , true                   },
      { BoolKey::AutoReconnectionEnabled  , true                   },
      { BoolKey::WaitForOutputBufferFlush , false                  },
      { BoolKey::FrameMarkerCommandEnabled, true                   },
      { BoolKey::SupportDisplayControl    , true                   },
      { BoolKey::SuppressOutput           , true                   },
  } };
}
auto ApplySettings(SettingsView settings, AuthMode auth, Rect picture) -> void {
  settings.Set(StringKey::AuthenticationPackageList, "!kerberos");
  settings.Apply(Flags(auth));
  settings.SetEncryptionLevel(EncryptionLevel::ClientCompatible);
  settings.Set(NumberKey::FrameAcknowledge, Narrowed<std::uint32_t>(AcknowledgedFrameWindow));
  settings.SetLargePointer({ .up_to_96x96 = true, .up_to_384x384 = true });
  ApplyDesktopSize(settings, picture);
}
auto BeginNegotiationLogging(SettingsReader settings) -> SettingsReader {
  ResetAuthenticationLogging();
  PeerNegotiationLogging(settings);
  return settings;
}
auto EndNegotiationLogging(SettingsReader /*settings*/) noexcept -> void {
  ResetAuthenticationLogging();
}
using NegotiationLogging = RAIIWrap<SettingsReader, BeginNegotiationLogging, EndNegotiationLogging>;
struct LiveConnection {
  std::reference_wrapper<Connection>    connection;
  std::reference_wrapper<SessionAccess> session;
};
auto Connect(Connection& connection, SessionAccess& session) -> LiveConnection {
  if (!connection.Initialize()) throw PeerSetupFailed{ "initialization" };
  return { .connection = connection, .session = session };
}
auto Disconnect(LiveConnection const& live) noexcept -> void {
  auto const held = live.session.get().Lock();
  live.connection.get().Disconnect();
}
using Live = RAIIWrap<LiveConnection, Connect, Disconnect>;
}
auto Peer::Serve(std::stop_token const& quit) -> void {
  std::stop_callback const wake(quit, [this] { _link.Signal(); });
  NegotiationLogging const logging { _link.Connection().Settings() };
  auto const               served  = [&] {
    Configure();
    Run(quit);
    return true;
  };
  auto const               failed  = [this](std::string_view failure) {
    _diagnostics.Log(LogLevel::Error, std::format("{} FreeRDP: {}.", failure, _link.Connection().Error().name));
  };
  std::ignore = Contained(false, served, failed);
  Depart();
}
auto Peer::Run(std::stop_token const& quit) -> void {
  Live const                                 live   { _link.Connection(), _session };
  std::array<WaitHandle, MaximumWaitHandles> handles{ };
  while (!quit.stop_requested() && Step(quit, handles)) {
  }
}
auto Peer::Configure() -> void {
  auto const picture  = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Picture(held); });
  auto const settings = _link.Connection().Settings();
  _authenticator.InstallCredentials(settings);
  ApplySettings(settings, _authenticator.Auth(), picture);
}
auto Peer::Step(std::stop_token const& quit, std::span<WaitHandle> handles) -> bool {
  auto const plan = [&] {
    auto const session = _session.Lock();
    return Plan(handles);
  }();
  return plan.count && Dispatch(quit, handles.first(plan.count), plan.timeout);
}
auto Peer::Dispatch(std::stop_token const& quit, std::span<WaitHandle> handles, std::uint32_t timeout) -> bool {
  auto const woke = WaitHandle::Any(handles, timeout);
  if (quit.stop_requested()) return false;
  Signalled const fired{ handles };
  // The handle the wait woke on goes last, so no handle starves the others.
  if (woke) std::ranges::rotate(handles, handles.subspan(*woke + 1).begin());
  return Service(quit, fired);
}
auto Peer::Plan(std::span<WaitHandle> handles) -> WaitPlan {
  _graphics.ExpireConfirmation();
  if (!_activation.Activated()) _link.Invalidate();
  auto const count = _link.Handles([&] { return CollectHandles(handles); });
  return { .count = count, .timeout = WaitTimeout() };
}
auto Peer::CollectHandles(std::span<WaitHandle> handles) -> std::uint32_t {
  Expects(handles.size() > AppendedHandleCount, "event array has room for transport and peer handles");
  auto const budget    = handles.first(handles.size() - AppendedHandleCount);
  auto const transport = _link.Connection().EventHandles(budget);
  if (transport.empty()) return 0;
  auto const rest = _channels.Handles(handles.subspan(transport.size()));
  Expects(rest.size() >= LoopHandleCount, "the loop's own handles fit");
  rest[0] = _link.Wake();
  rest[1] = _link.Channels().Handle();
  return Narrowed<std::uint32_t>(handles.size() - rest.size() + LoopHandleCount);
}
auto Peer::WaitTimeout() -> std::uint32_t {
  auto const blocked = _link.Connection().WriteBlocked();
  if (_activation.Holding()) {
    auto const remaining = _activation.ActivatedAt() + GraphicsConnectionWait - Activation::Clock::now();
    auto const wait      = WaitMilliseconds(remaining, 0);
    return blocked ? std::min(wait, BlockedRetry) : wait;
  }
  return blocked ? BlockedRetry : _pacing.Timeout();
}
auto Peer::Service(std::stop_token const& quit, Signalled const& ready) -> bool {
  auto const healthy = Exchange(quit, ready) && Deliver(quit);
  _traces.Flush();
  return healthy;
}
auto Peer::Exchange(std::stop_token const& quit, Signalled const& ready) -> bool {
  auto const session = _session.Lock();
  if (quit.stop_requested()) return false;
  if (!_link.Connection().Pump() || !_channels.Pump(ready)) return Ended();
  _redirection.Sound(ready);
  return _sender.Drain() || Ended();
}
auto Peer::Deliver(std::stop_token const& quit) -> bool {
  auto const delivery = _sender.Encode(quit);
  switch (delivery) {
  case Delivery::Healthy: return true;
  case Delivery::Stopped: return false;
  case Delivery::Failed: {
    auto const session = _session.Lock();
    return Ended();
  }
  default: Unreachable(delivery);
  }
}
auto Peer::Ended() -> bool {
  _end.Report();
  return false;
}
}
