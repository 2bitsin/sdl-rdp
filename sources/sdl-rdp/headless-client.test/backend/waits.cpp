#include <sdl-rdp/headless-client.test/backend/waits.hpp>

#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/settings.h>
#include <gtest/gtest.h>

namespace sdl_rdp::headless_client_test::backend::detail::waits {
using sdl_rdp::utilities::Expects;

namespace {
auto HasCookie(Client const& client) -> bool {
  Expects(client.Instance() != nullptr, "client instance exists");
  Expects(client.Instance()->context, "client instance has a context");
  auto const* cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(client.Instance()->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
}
auto AwaitAllAcknowledged(Client& client, BackendInstance const& backend, Logs& logs) -> void {
  ASSERT_TRUE(client.Until([&] { return AllAcknowledged(*backend); })) << logs.Text(true);
}
auto ConnectWithCookie(Client& client, Logs& logs) -> void {
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(UntilCookie(client));
}
auto UntilCookie(Client& client) -> bool {
  return client.Until([&] { return HasCookie(client); });
}
auto UntilLogged(Client& client, Logs& logs, std::string_view text, std::chrono::milliseconds timeout) -> bool {
  return client.Until([&] { return logs.Contains(text); }, timeout);
}
}
