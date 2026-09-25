#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/settings.h>

namespace sdl_rdp::headless_client_test::client::detail::has_cookie {
using sdl_rdp::utilities::Expects;

auto HasCookie(Client& client) -> bool {
  Expects(client.Instance() != nullptr, "client instance exists");
  Expects(client.Instance()->context, "client instance has a context");
  auto const* cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(client.Instance()->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
}
