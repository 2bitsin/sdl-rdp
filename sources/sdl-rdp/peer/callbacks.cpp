#include <sdl-rdp/peer/callbacks.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/peer/activator.hpp>
#include <sdl-rdp/peer/capability-check.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/video/output-control.hpp>

#include <freerdp/update.h>
#include <cstdint>

namespace Backend {
namespace {
auto PeerOwner(freerdp_peer const& client) -> PeerCallbacks& {
  return CallbackOwner<PeerCallbacks, &freerdp_peer::ContextExtra>(client);
}
auto ContextOwner(rdpContext const& context) -> PeerCallbacks& {
  Expects(context.peer != nullptr, "the callback context has its peer");
  return PeerOwner(*context.peer);
}
constexpr OperationName PeerActivation      { "Peer activation"               };
constexpr OperationName PeerCapabilities    { "Peer capabilities"             };
constexpr OperationName PeerLogon           { "Peer logon"                    };
constexpr OperationName NtlmHash            { "NTLM hash"                     };
constexpr OperationName FrameAcknowledgement{ "Surface frame acknowledgement" };
constexpr OperationName SuppressOutput      { "Suppress output"               };
using sdl_rdp::freerdp_facade::Handled;
}
PeerCallbacks::PeerCallbacks(PeerLink& link, Authenticator& authenticator, Activator& activator,
                             CapabilityCheck& capabilities, OutputControl& output, InputEvents& input)
    : _link{ link }, _authenticator{ authenticator }, _activator{ activator }, _capabilities{ capabilities },
      _output{ output }, _input{ input } {
  _link.Client().ContextExtra = this;
  InstallClient();
  InstallUpdates();
  input.Install(*_link.Context().input);
}
auto PeerCallbacks::InstallClient() -> void {
  auto& client = _link.Client();
  // abi: psPeerActivate, psPeerCapabilities, psPeerPostConnect; BOOL is int
  client.Activate     = Handled<PeerOwner, &PeerCallbacks::Activate, PeerActivation, _failures, false>;
  client.Capabilities = Handled<PeerOwner, &PeerCallbacks::Capabilities, PeerCapabilities, _failures, false>;
  // PostConnect has no work that can fail: the session starts at Activate.
  client.PostConnect = [](freerdp_peer*) noexcept -> int { return true; };
  InstallAuthentication();
}
auto PeerCallbacks::InstallAuthentication() -> void {
  auto&          client = _link.Client();
  constexpr auto logon  = [](PeerCallbacks& owner, SEC_WINNT_AUTH_IDENTITY const& /*identity*/, int automatic) {
    return owner._authenticator.Logon(automatic != 0);
  };
  // FreeRDP 3.32 ntlm_compute.c:513 passes the 16-byte hash buffer by its first byte, every other argument set.
  constexpr auto hash = [](PeerCallbacks& owner, SEC_WINNT_AUTH_IDENTITY const& identity, SecBuffer const&,
                           std::uint8_t const&, std::uint8_t const&, SecBuffer const&, NtKey response) -> std::int32_t {
    return owner._authenticator.Hash(identity, response) ? SEC_E_OK : SEC_E_LOGON_DENIED;
  };
  // abi: psPeerLogon, BOOL is int; psSspiNtlmHashCallback, SECURITY_STATUS is LONG, an int32_t
  client.Logon                = Handled<PeerOwner, logon, PeerLogon, _failures, false>;
  client.SspiNtlmHashCallback = Handled<PeerOwner, hash, NtlmHash, _failures, SEC_E_INTERNAL_ERROR>;
}
auto PeerCallbacks::InstallUpdates() -> void {
  auto& update = *_link.Context().update;
  // abi: pSurfaceFrameAcknowledge, pSuppressOutput; BOOL is int
  update.SurfaceFrameAcknowledge = Handled<ContextOwner, &PeerCallbacks::Acknowledge, FrameAcknowledgement, _failures,
                                           false>;
  update.SuppressOutput          = Handled<ContextOwner, &PeerCallbacks::Suppress, SuppressOutput, _failures, false>;
}
auto PeerCallbacks::Activate() -> bool {
  return _activator.Activate();
}
auto PeerCallbacks::Capabilities() -> bool {
  return _capabilities.Accept();
}
auto PeerCallbacks::Acknowledge(std::uint32_t id) -> bool {
  _output.Acknowledge(id);
  return true;
}
auto PeerCallbacks::Suppress(std::uint8_t allow) -> bool {
  _output.Suppress(allow != 0);
  return true;
}
PeerCallbacks::~PeerCallbacks() {
  _link.Client().ContextExtra = nullptr;
}
auto PeerCallbacks::FailureSource() const noexcept -> InputEvents const& {
  return _input;
}
}
