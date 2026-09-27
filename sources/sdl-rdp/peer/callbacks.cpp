#include <sdl-rdp/peer/peer.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/peer/arrival.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/output-control.hpp>

#include <freerdp/session.h>
#include <freerdp/update.h>
#include <winpr/crypto.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::auth::NtKey;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::picture::ApplyDesktopSize;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OperationName;

namespace {
using sdl_rdp::freerdp_facade::Handled;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::ReconnectCookie;
constexpr OperationName                PeerActivation       { "Peer activation"               };
constexpr OperationName                PeerCapabilities     { "Peer capabilities"             };
constexpr OperationName                PeerLogon            { "Peer logon"                    };
constexpr OperationName                NtlmHash             { "NTLM hash"                     };
constexpr OperationName                FrameAcknowledgement { "Surface frame acknowledgement" };
constexpr OperationName                SuppressOutput       { "Suppress output"               };
constexpr std::uint32_t                SessionLogonId       = 1;
constexpr std::array<std::uint32_t, 3> ColourDepths         { 16, 24, 32                      };
auto PeerOwner(freerdp_peer const& client) -> Peer& {
  return CallbackOwner<Peer, &freerdp_peer::ContextExtra>(client);
}
auto ContextOwner(rdpContext const& context) -> Peer& {
  Expects(context.peer != nullptr, "the callback context has its peer");
  return PeerOwner(*context.peer);
}
auto SendCookie(PeerLink& link) -> bool {
  ReconnectCookie cookie{ .logon_id = SessionLogonId };
  if (winpr_RAND(cookie.random_bits.data(), cookie.random_bits.size()) != 0) return false;
  link.Settings().SetAutoReconnectCookie(cookie);
  logon_info_ex info{ };
  info.haveCookie = true;
  info.LogonId    = cookie.logon_id;
  std::ranges::copy(cookie.random_bits, info.ArcRandomBits);
  auto& context = link.Context();
  return context.update->SaveSessionInfo(&context, INFO_TYPE_LOGON_EXTENDED_INF, &info);
}
}
auto Peer::InstallClient() -> void {
  auto& client = _link.Client();
  // abi: psPeerActivate, psPeerCapabilities, psPeerPostConnect; BOOL is int
  client.Activate     = Handled<PeerOwner, &Peer::Activate, PeerActivation, _failures, false>;
  client.Capabilities = Handled<PeerOwner, &Peer::AcceptCapabilities, PeerCapabilities, _failures, false>;
  // PostConnect has no work that can fail: the session starts at Activate.
  client.PostConnect = [](freerdp_peer*) noexcept -> int { return true; };
  InstallAuthentication();
}
auto Peer::InstallAuthentication() -> void {
  auto&          client = _link.Client();
  constexpr auto logon  = [](Peer& owner, SEC_WINNT_AUTH_IDENTITY const& /*identity*/, int automatic) {
    return owner._authenticator.Logon(automatic != 0);
  };
  // FreeRDP 3.32 ntlm_compute.c:513 passes the 16-byte hash buffer by its first byte, every other argument set.
  constexpr auto hash = [](Peer& owner, SEC_WINNT_AUTH_IDENTITY const& identity, SecBuffer const&, std::uint8_t const&,
                           std::uint8_t const&, SecBuffer const&, NtKey response) -> std::int32_t {
    return owner._authenticator.Hash(identity, response) ? SEC_E_OK : SEC_E_LOGON_DENIED;
  };
  // abi: psPeerLogon, BOOL is int; psSspiNtlmHashCallback, SECURITY_STATUS is LONG, an int32_t
  client.Logon                = Handled<PeerOwner, logon, PeerLogon, _failures, false>;
  client.SspiNtlmHashCallback = Handled<PeerOwner, hash, NtlmHash, _failures, SEC_E_INTERNAL_ERROR>;
}
auto Peer::InstallUpdates() -> void {
  auto& update = *_link.Context().update;
  // abi: pSurfaceFrameAcknowledge, pSuppressOutput; BOOL is int
  update.SurfaceFrameAcknowledge = Handled<ContextOwner, &Peer::Acknowledge, FrameAcknowledgement, _failures, false>;
  update.SuppressOutput          = Handled<ContextOwner, &Peer::Suppress, SuppressOutput, _failures, false>;
}
auto Peer::Activate() -> bool {
  if (_activation.Active()) {
    _link.Signal();
    return true;
  }
  if (!_authenticator.VerifySettings() || !SendCookie(_link)) return false;
  if (!_encoder.Select(_link.Settings(), _configuration.CodecPreference())) return false;
  _arrival.Admit(_encoder.SelectedCodec());
  return true;
}
auto Peer::AcceptCapabilities() -> bool {
  if (!_authenticator.VerifySettings()) return false;
  auto const settings = _link.Settings();
  auto const frame    = _store.Lock();
  if (!_activation.Activated()) {
    _pacing.Restart(frame);
    _desktop.RecordScreen(settings);
  }
  if (!std::ranges::contains(ColourDepths, settings.Get(NumberKey::ColorDepth))) {
    _diagnostics.Log(LogLevel::Warn, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return false;
  }
  ApplyDesktopSize(settings, _desktop.Offer(_store.Picture(frame)));
  return true;
}
auto Peer::Acknowledge(std::uint32_t id) -> bool {
  _output.Acknowledge(id);
  return true;
}
auto Peer::Suppress(std::uint8_t allow) -> bool {
  _output.Suppress(allow != 0);
  return true;
}
auto Peer::FailureSource() const noexcept -> InputEvents const& {
  return _input_events;
}
}
