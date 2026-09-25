#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>

#include <sdl-rdp/headless-client.test/backend/status.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::headless_client_test::backend::detail::await_acknowledged {
auto AwaitAllAcknowledged(Client& client, BackendInstance const& backend, Logs& logs) -> void {
  ASSERT_TRUE(client.Until([&] { return AllAcknowledged(*backend); })) << logs.Text(true);
}
}
