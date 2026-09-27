#include <sdl-rdp/headless-client.test/backend/waits.hpp>

#include <sdl-rdp/headless-client.test/backend/status.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::headless_client_test::backend::detail::waits {
using sdl_rdp::headless_client_test::client::SettingsOf;

namespace {
auto HasCookie(Client& client) -> bool {
  return SettingsOf(client).AutoReconnectCookie().has_value();
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
