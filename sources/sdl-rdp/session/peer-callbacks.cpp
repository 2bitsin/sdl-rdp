#include <sdl-rdp/session/peer-callbacks.hpp>

#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/core/failure-log.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/input/input-events.hpp>
#include <sdl-rdp/session/activator.hpp>
#include <sdl-rdp/session/capability-check.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/video/output-control.hpp>

#include <freerdp/update.h>
#include <cstdint>

namespace Backend {
namespace {
auto Router(freerdp_peer* client) -> PeerCallbacks& {
  Expects(client != nullptr, "client transport exists");
  return CallbackOwner<PeerCallbacks>(client->ContextExtra);
}
auto Router(rdpContext* context) -> PeerCallbacks& {
  Expects(context != nullptr, "callback context exists");
  return Router(context->peer);
}
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
  client.Activate     = [](freerdp_peer* peer) noexcept -> int {
    auto& owner = Router(peer);
    return Contained(false, [&] { return owner._activator.Activate(); }, owner.Failures("Peer activation"));
  };
  client.Capabilities = [](freerdp_peer* peer) noexcept -> int {
    auto& owner = Router(peer);
    return Contained(false, [&] { return owner._capabilities.Accept(); }, owner.Failures("Peer capabilities"));
  };
  // PostConnect has no work that can fail: the session starts at Activate.
  client.PostConnect = [](freerdp_peer*) noexcept -> int { return true; };
  InstallAuthentication();
}
auto PeerCallbacks::InstallAuthentication() -> void {
  auto& client = _link.Client();
  // abi: psPeerLogon, BOOL is int
  client.Logon = [](freerdp_peer* peer, SEC_WINNT_AUTH_IDENTITY const*, int automatic) noexcept -> int {
    auto& owner = Router(peer);
    return Contained(false, [&] { return owner._authenticator.Logon(automatic != 0); }, owner.Failures("Peer logon"));
  };
  // abi: psSspiNtlmHashCallback, BYTE is uint8_t, SECURITY_STATUS is LONG, an int32_t
  client.SspiNtlmHashCallback = [](void* peer, SEC_WINNT_AUTH_IDENTITY const* identity, SecBuffer const*,
                                   std::uint8_t const*, std::uint8_t const*, SecBuffer const*,
                                   std::uint8_t* response) noexcept -> std::int32_t {
    Expects(identity != nullptr, "NTLM identity is supplied");
    Expects(response != nullptr, "callback response is supplied");
    auto&      owner = Router(static_cast<freerdp_peer*>(peer));
    auto const hash  = [&] { return owner._authenticator.Hash(*identity, response) ? SEC_E_OK : SEC_E_LOGON_DENIED; };
    return Contained(SEC_E_INTERNAL_ERROR, hash, owner.Failures("NTLM hash"));
  };
}
auto PeerCallbacks::InstallUpdates() -> void {
  auto& update = *_link.Context().update;
  // abi: pSurfaceFrameAcknowledge, pSuppressOutput; BOOL is int
  update.SurfaceFrameAcknowledge = [](rdpContext* context, std::uint32_t id) noexcept -> int {
    auto&      owner       = Router(context);
    auto const acknowledge = [&] {
      owner._output.Acknowledge(id);
      return true;
    };
    return Contained(false, acknowledge, owner.Failures("Surface frame acknowledgement"));
  };
  update.SuppressOutput          = [](rdpContext* context, std::uint8_t allow, RECTANGLE_16 const*) noexcept -> int {
    auto&      owner    = Router(context);
    auto const suppress = [&] {
      owner._output.Suppress(allow != 0);
      return true;
    };
    return Contained(false, suppress, owner.Failures("Suppress output"));
  };
}
auto PeerCallbacks::Failures(OperationName operation) const noexcept -> FailureLog {
  return _input.Failures(operation);
}
PeerCallbacks::~PeerCallbacks() {
  _link.Client().ContextExtra = nullptr;
}
}
