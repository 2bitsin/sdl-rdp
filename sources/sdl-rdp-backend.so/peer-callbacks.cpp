#include "_detail/peer-callbacks.hpp"

#include "_detail/activator.hpp"
#include "_detail/auth.hpp"
#include "_detail/callback-owner.hpp"
#include "_detail/capability-check.hpp"
#include "_detail/input-events.hpp"
#include "_detail/output-control.hpp"
#include "_detail/peer-link.hpp"

#include <freerdp/update.h>

namespace Backend {
namespace {
PeerCallbacks& Router(freerdp_peer* client) {
  Expects(client != nullptr, "client transport exists");
  return CallbackOwner<PeerCallbacks>(client->ContextExtra);
}
PeerCallbacks& Router(rdpContext* context) {
  Expects(context != nullptr, "callback context exists");
  return Router(context->peer);
}
}
PeerCallbacks::PeerCallbacks(PeerLink& link, Authenticator& authenticator, Activator& activator,
                             CapabilityCheck& capabilities, OutputControl& output, InputEvents& input)
    : _link { link }, _authenticator{ authenticator }, _activator{ activator }, _capabilities{ capabilities },
      _output{ output } {
  _link.Client().ContextExtra = this;
  InstallClient();
  InstallUpdates();
  input.Install(*_link.Context().input);
}
void PeerCallbacks::InstallClient() {
  auto& client = _link.Client();
  client.Activate             = [](freerdp_peer* peer) { return Router(peer)._activator.Activate(); };
  client.Capabilities         = [](freerdp_peer* peer) { return Router(peer)._capabilities.Accept(); };
  client.PostConnect          = [](freerdp_peer*) -> BOOL { return TRUE; };
  client.Logon                = [](freerdp_peer* peer, SEC_WINNT_AUTH_IDENTITY const*, BOOL automatic) {
    return Router(peer)._authenticator.Logon(automatic);
  };
  client.SspiNtlmHashCallback = [](void* peer, SEC_WINNT_AUTH_IDENTITY const* identity, SecBuffer const*,
                                   BYTE const*, BYTE const*, SecBuffer const*, BYTE* response) -> SECURITY_STATUS {
    Expects(identity != nullptr, "NTLM identity is supplied");
    Expects(response != nullptr, "callback response is supplied");
    return Router(static_cast<freerdp_peer*>(peer))._authenticator.Hash(*identity, response) ? 1 : 0;
  };
}
void PeerCallbacks::InstallUpdates() {
  auto& update = *_link.Context().update;
  update.SurfaceFrameAcknowledge = [](rdpContext* context, UINT32 id) -> BOOL {
    Router(context)._output.Acknowledge(id);
    return TRUE;
  };
  update.SuppressOutput          = [](rdpContext* context, BYTE allow, RECTANGLE_16 const*) -> BOOL {
    Router(context)._output.Suppress(allow != 0);
    return TRUE;
  };
}
PeerCallbacks::~PeerCallbacks() {
  _link.Client().ContextExtra = nullptr;
}
}
