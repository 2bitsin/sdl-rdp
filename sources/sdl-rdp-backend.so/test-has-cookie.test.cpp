#include "_detail/test-has-cookie.hpp"

#include "_detail/contract.hpp"

#include <freerdp/settings.h>

namespace BackendGate {
bool HasCookie(Headless::Client const& client) {
  utilities::Expects(client.Instance() != nullptr, "client instance exists");
  utilities::Expects(client.Instance()->context, "client instance has a context");
  auto const* cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(client.Instance()->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
}
