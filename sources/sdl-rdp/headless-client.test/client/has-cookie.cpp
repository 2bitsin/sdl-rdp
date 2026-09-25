#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/settings.h>

namespace BackendGate {
auto HasCookie(Headless::Client const& client) -> bool {
  utilities::Expects(client.Instance() != nullptr, "client instance exists");
  utilities::Expects(client.Instance()->context, "client instance has a context");
  auto const* cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(client.Instance()->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
}
